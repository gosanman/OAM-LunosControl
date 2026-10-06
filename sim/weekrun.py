"""Wochenlauf: sieben Tage beschleunigt, ohne Oberflaeche, mit Fehlerprotokoll.

Rechnet den echten Firmware-Code mit Umgebung und Bewohner Minute fuer Minute
durch, baut auf Wunsch Stoerungen ein und schreibt einen Bericht nach
build/reports/. Gedacht als Dauertest: was hier auffaellt, faellt auch am
Geraet auf - nur nach einer Woche statt nach einer Minute.

    python sim/weekrun.py --project "Aufbau B (Testplan)" --season Winter
    python sim/weekrun.py --project "Bad mit ego + e2 60-Paar" --days 7 --no-disturb

Geprueft wird laufend:
  Waechter     Invarianten 3/5/6 und DPT-Fehler aus dem Kern, Fehlerzeilen der Firmware
  Plausibilitaet  gemeldete Stufe/Richtung je Luefter gegen die Spannung am DAC
  Wirkung      kommt eine Bedienung des Bewohners im Raum an (Betriebsart, Nacht,
               Handstufe, Fenster/Sperre, Taste)?
  Erholung     nach jeder Stoerung (12 V, KNX-Bus, ETS-Download): Fehler weg,
               Luefter laufen wieder
  Statistik    Raumluft, Stufen, Fehlercodes, Pendelwechsel, Buslast
"""

import argparse
import collections
import json
import os
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import kwlsim  # noqa: E402

MIN = 60000
ERR = {1: 'Freigabe fehlt', 2: 'Master-Timeout', 3: 'Konfiguration', 4: 'DAC nicht erreichbar',
       5: 'ungültiger Empfangswert', 6: 'suspendiert', 7: 'Feuchtevergleich sperrt',
       8: 'Messwerte fehlen', 9: 'Schutzbetrieb', 10: 'Filterwechsel fällig', 11: 'Richtungskonflikt'}
WEEKDAY = ['Mo', 'Di', 'Mi', 'Do', 'Fr', 'Sa', 'So']
BELOW_5V_IS_SUPPLY = True      # Board-Header FANDRV_BELOW_5V_IS_SUPPLY = 1


def when(env, t):
    h = env.tod_hours(t)
    return f"{WEEKDAY[env.day(t) % 7]} {int(h):02d}:{int(h % 1 * 60):02d}"


class Recorder:
    def __init__(self, sim, days):
        self.sim, self.days = sim, days
        self.warns = collections.OrderedDict()      # key -> dict
        self.log_errors = collections.Counter()
        self.log_first = {}
        self.log_times = collections.defaultdict(list)
        self.occ = collections.Counter()
        self.occ_list = []
        self.checks = []                             # offene Wirkungspruefungen
        self.effect = collections.defaultdict(lambda: [0, 0])   # Art -> [ok, ohne Wirkung]
        self.effect_examples = collections.defaultdict(list)
        self.fan_stage = collections.defaultdict(collections.Counter)
        self.fan_err = collections.defaultdict(collections.Counter)
        self.fan_changes = collections.Counter()
        self.last_stage_ko = {}
        self.mismatch = collections.Counter()
        self.code7_dry = collections.Counter()   # Code 7 bei rF unter 45 %
        self.mismatch_ex = []
        self.room = collections.defaultdict(lambda: dict(co2max=0, rhmax=0, tmin=99, tmax=-99,
                                                          co2_1500=0, rh_70=0, t_17=0, n=0,
                                                          co2sum=0.0, rhsum=0.0, tsum=0.0))
        self.room_stage = collections.defaultdict(collections.Counter)
        self.room_mode = collections.defaultdict(collections.Counter)
        self.room_src = collections.defaultdict(collections.Counter)
        self.tel_hour = collections.Counter()
        self.fullload = []
        self.boots = 0
        self.disturb = []
        self.seq = 0

    # ------------------------------------------------------------ Ereignisse
    def events(self):
        sim = self.sim
        new = [e for s, e in sim.events if s > self.seq]
        self.seq = sim.seq
        env = sim.env
        for ev in new:
            k = ev.get('ev')
            t = ev.get('t', 0)
            if k == 'warn':
                w = self.warns.setdefault(ev['key'], dict(inv=ev['inv'], text=ev['text'], first=t, count=0))
                w['count'] = max(w['count'], ev.get('count', 1))
            elif k == 'log' and ev.get('level') == 'error':
                text = f"{ev.get('src', '')}: {ev['text'].strip()}"
                self.log_errors[text] += 1
                self.log_first.setdefault(text, t)
                self.log_times[text].append(t)
            elif k == 'info' and 'Volllast' in ev.get('text', ''):
                self.fullload.append((t, ev['text']))
            elif k == 'boot':
                self.boots += 1
            elif k in ('ko', 'rx'):
                self.tel_hour[(t // 3600000, k)] += 1
                if k == 'ko':
                    nr = ev['nr']
                    if 340 <= nr < 340 + 12 * 24 and (nr - 340) % 24 == 6:      # FAN_nStageAct
                        # Nur echte Stufenwechsel: die 0 in jeder Totzeit zaehlt nicht.
                        fan = (nr - 340) // 24 + 1
                        if ev['val']:
                            if self.last_stage_ko.get(fan) not in (None, ev['val']):
                                self.fan_changes[fan] += 1
                            self.last_stage_ko[fan] = ev['val']
            elif k == 'occupant':
                text = ev['text']
                kind = text.split(':')[0].split('(')[0].strip()
                self.occ[kind] += 1
                self.occ_list.append((t, ev['room'], text))
                self._expect(t, ev['room'], text)

    def _expect(self, t, room, text):
        """Was muss nach dieser Bedienung im Raum zu sehen sein?"""
        if 'Betriebsart Komfort' in text:
            self.checks.append((t + MIN, room, 'Betriebsart Komfort', lambda r: r['mode'] == 'Komfort' or r['modeRank'] < 10))
        elif 'Betriebsart Standby' in text:
            self.checks.append((t + MIN, room, 'Betriebsart Standby', lambda r: r['mode'] == 'Standby' or r['modeRank'] < 10))
        elif 'Nacht ein' in text:
            self.checks.append((t + MIN, room, 'Nacht ein', lambda r: r['modeRank'] <= 8))
        elif text.startswith('Handstufe'):
            self.checks.append((t + MIN, room, 'Handstufe 3', lambda r: r['source'].startswith(('5', '1', '2', '3')) and
                                (r['stage'] == 3 or not r['source'].startswith('5'))))
        elif 'Fenster auf' in text:
            self.checks.append((t + MIN, room, 'Fenster auf → Sperre', lambda r: r['source'].startswith('1')))
        elif 'Stoßlüften ein' in text:
            self.checks.append((t + MIN, None, 'Taste Stoßlüften', lambda r: r['mode'] == 'Stosslueften' or r['modeRank'] < 9))

    # ------------------------------------------------------------ Stichprobe
    def sample(self):
        sim = self.sim
        st = sim.state
        t = st['t']
        s = st['sim']
        rooms = {r['no']: r for r in s['rooms']}
        # Wirkungspruefungen faellig?
        keep = []
        for due, room, kind, ok in self.checks:
            if t < due:
                keep.append((due, room, kind, ok))
                continue
            targets = [rooms[room]] if room in rooms else list(rooms.values())
            good = all(ok(r) for r in targets) if targets else False
            self.effect[kind][0 if good else 1] += 1
            if not good and len(self.effect_examples[kind]) < 3:
                r = targets[0] if targets else {}
                self.effect_examples[kind].append(
                    f"{when(sim.env, due - MIN)} Raum {room or 'alle'}: danach Betriebsart {r.get('mode')} "
                    f"(Rang {r.get('modeRank')}), Stufe {r.get('stage')} aus {r.get('source')}")
        self.checks = keep

        if not st.get('mcu'):
            return
        dac = s['dac']
        env_rh = {no: e['RH'] for no, e in sim.env.status.items()}
        for f in s['fans']:
            self.fan_stage[f['no']][f['stage']] += 1
            self.fan_err[f['no']][f['err']] += 1
            if f['err'] == 7 and env_rh.get(f['room'], 100) < 45:
                self.code7_dry[f['no']] += 1
            # Plausibilitaet: Status gegen Spannung (bipolar, erster Motor)
            if f['bipolar'] and dac[f['ch'] - 1]['powered'] and not (f['type'] == 'ego' and f['stage'] == 4):
                v = dac[f['ch'] - 1]['volt']
                if f['stage'] == 0 or abs(v - 5.0) < 0.06:
                    # Stufe 0, Totzeit oder Totzeit-Halt je Kanal: 5,00 V ist richtig.
                    good = abs(v - 5.0) < 0.06
                else:
                    below = v < 5.0
                    good = below == ((f['dir'] == 'Zuluft') == BELOW_5V_IS_SUPPLY) and abs(v - 5.0) > 0.2
                if not good:
                    self.mismatch[f['no']] += 1
                    if len(self.mismatch_ex) < 5:
                        self.mismatch_ex.append(f"{when(sim.env, t)} Lüfter {f['no']}: Status Stufe {f['stage']} "
                                                f"{f['dir']}, am DAC S{f['ch']} {v:.2f} V")
        for no, r in rooms.items():
            self.room_stage[no][r['stage']] += 1
            self.room_mode[no][f"{r['mode']} (Rang {r['modeRank']})"] += 1
            self.room_src[no][r['source']] += 1
        for no, e in sim.env.status.items():
            a = self.room[no]
            a['profile'] = e['profile']
            a['n'] += 1
            a['co2max'] = max(a['co2max'], e['CO2'])
            a['rhmax'] = max(a['rhmax'], e['RH'])
            a['tmin'] = min(a['tmin'], e['T'])
            a['tmax'] = max(a['tmax'], e['T'])
            a['co2_1500'] += e['CO2'] > 1500
            a['rh_70'] += e['RH'] > 70
            a['t_17'] += e['T'] < 17
            a['co2sum'] += e['CO2']
            a['rhsum'] += e['RH']
            a['tsum'] += e['T']


def disturb_plan(days):
    """Stoerungen: (Minute, Name, Aktionen)."""
    plan = []
    if days > 1:
        plan.append((1 * 1440 + 14 * 60, '12 V fallen für 5 s aus', [('v12', 0), ('run', 5000), ('v12', 1)]))
    if days > 3:
        plan.append((3 * 1440 + 3 * 60, 'KNX-Bus fällt für 60 s aus', [('bus', 0), ('run', 60000), ('bus', 1)]))
    if days > 4:
        plan.append((4 * 1440 + 10 * 60, 'ETS-Download (Neuprogrammierung)', [('program', None)]))
    if days > 5:
        plan.append((5 * 1440 + 19 * 60, '12 V kurz weg (300 ms)', [('v12', 0), ('run', 300), ('v12', 1)]))
    return plan


def run(project, days, season, seed, disturb, out_dir):
    t0 = time.time()
    sim = kwlsim.Simulator(project)
    sim.action({'action': 'env', 'patch': {'enabled': True, 'season': season, 'startHour': 0, 'seed': seed}})
    sim.action({'action': 'occupant', 'patch': {'enabled': True, 'seed': seed + 1}})
    rec = Recorder(sim, days)
    plan = {m: (name, steps) for m, name, steps in (disturb_plan(days) if disturb else [])}

    for minute in range(days * 1440):
        if minute in plan:
            name, steps = plan[minute]
            t_start = sim.state['t']
            for op, arg in steps:
                if op in ('v12', 'bus'):
                    sim.action({'action': 'power', 'which': op, 'on': bool(arg)})
                elif op == 'run':
                    sim.run(arg)
                elif op == 'program':
                    sim.action({'action': 'program'})
            rec.disturb.append(dict(t=t_start, name=name, check_at=sim.state['t'] + 3 * MIN))
        sim.run(MIN)
        rec.events()
        rec.sample()
        for d in rec.disturb:
            if 'result' not in d and sim.state['t'] >= d['check_at']:
                s = sim.state['sim']
                d['result'] = dict(mcu=sim.state['mcu'], error=s['module']['error'],
                                   fans=[(f['no'], f['stage'], f['err']) for f in s['fans']],
                                   dac=[round(x['volt'], 2) for x in s['dac']])
        if minute % 1440 == 1439:
            print(f'  Tag {minute // 1440 + 1} fertig ({time.time() - t0:.0f} s)', flush=True)

    final = sim.state
    sim.core.close()
    return report(project, days, season, seed, disturb, rec, final, time.time() - t0, out_dir)


def report(project, days, season, seed, disturb, rec, final, secs, out_dir):
    L = []
    p = L.append
    p(f'# Wochenlauf: {project}')
    p('')
    p(f'{days} Tage, Jahreszeit {season}, Startwert {seed}, Störungen {"ja" if disturb else "nein"}, '
      f'Rechenzeit {secs:.0f} s. Firmware: unveränderter Code aus lib/OFM-KwlControl.')
    p('')

    # --- Befunde ------------------------------------------------------------
    findings = []
    for w in rec.warns.values():
        findings.append(f"**{w['inv']}** ({w['count']}×, zuerst {fmt_t(w['first'])}): {w['text']}")
    for kind, (ok, bad) in rec.effect.items():
        if bad:
            findings.append(f'**Bedienung ohne Wirkung: {kind}** – {bad} von {ok + bad} Mal. '
                            + ' · '.join(rec.effect_examples[kind]))
    if sum(rec.mismatch.values()):
        findings.append(f'**Status passt nicht zur Spannung** – {sum(rec.mismatch.values())} Minuten. '
                        + ' · '.join(rec.mismatch_ex))
    if sum(rec.code7_dry.values()):
        findings.append('**Fehlercode 7 ohne Feuchteanforderung** – ' + ', '.join(
            f'Lüfter {no}: {n} min' for no, n in sorted(rec.code7_dry.items())) +
            ' mit „Feuchtevergleich sperrt“, obwohl die Raumfeuchte unter 45 % lag. Weil der kleinste Code '
            'gewinnt, verdeckt die 7 in dieser Zeit die Codes 8 (Messwerte fehlen), 9 (Schutz), 10 (Filter) '
            'und 11 (Richtungskonflikt).')
    # Buslast: Befund erst ab 250 gesendeten Telegrammen je Stunde und Lüfter.
    fans = max(1, len(rec.fan_stage))
    if hours_total(rec) > 250 * fans:
        findings.append(f'**Buslast** – im Mittel {hours_total(rec):.0f} gesendete Telegramme je Stunde '
                        f'bei {fans} Lüftern.')
    # Fehlerzeilen in den ersten 2 min nach einer eingebauten Störung sind die
    # erwartete Reaktion darauf (z. B. Code 4 bei 12 V aus) - kein Befund.
    expected = lambda t: any(0 <= t - d['t'] <= 2 * MIN for d in rec.disturb)
    for text, n in rec.log_errors.items():
        if 'Ohne Parametrierung' in text:
            continue
        times = rec.log_times.get(text, [])
        if times and all(expected(t) for t in times):
            continue
        findings.append(f'**Firmware-Fehlerzeile** ({n}×, zuerst {fmt_t(rec.log_first[text])}): {text}')
    p('## Befunde')
    p('')
    if findings:
        for f in findings:
            p(f'- {f}')
    else:
        p('Keine.')
    p('')

    # --- Stoerungen -------------------------------------------------------------
    if rec.disturb:
        p('## Störungen und Erholung')
        p('')
        p('| Zeit | Störung | 3 min danach |')
        p('|---|---|---|')
        for d in rec.disturb:
            r = d.get('result', {})
            fans = ', '.join(f'L{no} Stufe {st}{" Fehler " + str(e) if e else ""}' for no, st, e in r.get('fans', []))
            okay = r.get('mcu') and r.get('error') == 0 and all(e in (0, 7, 8, 9, 10) for _, _, e in r.get('fans', []))
            p(f"| {fmt_t(d['t'])} | {d['name']} | {'erholt' if okay else 'NICHT erholt'}: Modulfehler {r.get('error')}, {fans} |")
        p('')

    # --- Wirkung der Bedienung -----------------------------------------------------
    p('## Bedienung durch den Bewohner')
    p('')
    p('| Aktion | Anzahl |')
    p('|---|---|')
    for kind, n in rec.occ.most_common():
        p(f'| {kind} | {n} |')
    p('')
    p('| Prüfung | angekommen | ohne Wirkung |')
    p('|---|---|---|')
    for kind, (ok, bad) in rec.effect.items():
        p(f'| {kind} | {ok} | {bad} |')
    p('')

    # --- Raumluft --------------------------------------------------------------
    p('## Raumluft')
    p('')
    p('| Raum | Profil | CO₂ Mittel / max | Minuten > 1500 ppm | rF Mittel / max | Minuten > 70 % | T min … max | Minuten < 17 °C |')
    p('|---|---|---|---|---|---|---|---|')
    for no, a in sorted(rec.room.items()):
        n = max(1, a['n'])
        prof = a.get('profile', '')
        p(f"| {no} | {prof} | {a['co2sum'] / n:.0f} / {a['co2max']:.0f} | {a['co2_1500']} | "
          f"{a['rhsum'] / n:.0f} / {a['rhmax']:.0f} | {a['rh_70']} | {a['tmin']:.1f} … {a['tmax']:.1f} | {a['t_17']} |")
    p('')
    p('| Raum | Stufe 0/1/2/3/4 (Anteil der Zeit) | Herkunft der Stufe | Betriebsart (Rang) |')
    p('|---|---|---|---|')
    for no in sorted(rec.room_stage):
        c = rec.room_stage[no]
        tot = max(1, sum(c.values()))
        stages = ' / '.join(f'{100 * c[s] / tot:.0f} %' for s in range(5))
        src = ', '.join(f'{k} {100 * v / tot:.0f} %' for k, v in rec.room_src[no].most_common(4))
        modes = ', '.join(f'{k} {100 * v / tot:.0f} %' for k, v in rec.room_mode[no].most_common(4))
        p(f'| {no} | {stages} | {src} | {modes} |')
    p('')

    # --- Luefter ----------------------------------------------------------------
    p('## Lüfter')
    p('')
    p('| Lüfter | Stufe 0/1/2/3/4 (Anteil) | Stufenwechsel je Tag | Fehlercodes (Minuten) | Betriebsstunden |')
    p('|---|---|---|---|---|')
    fans_final = {f['no']: f for f in final.get('sim', {}).get('fans', [])}
    for no in sorted(rec.fan_stage):
        c = rec.fan_stage[no]
        tot = max(1, sum(c.values()))
        stages = ' / '.join(f'{100 * c[s] / tot:.0f} %' for s in range(5))
        errs = ', '.join(f'{k} {ERR.get(k, "")} {v}' for k, v in sorted(rec.fan_err[no].items()) if k) or '—'
        p(f"| {no} | {stages} | {rec.fan_changes[no] / days:.0f} | {errs} | {fans_final.get(no, {}).get('runHours', '—')} |")
    p('')

    # --- Bus ------------------------------------------------------------------------
    hours = collections.defaultdict(lambda: [0, 0])
    for (h, k), n in rec.tel_hour.items():
        hours[h][0 if k == 'ko' else 1] += n
    if hours:
        tx = [v[0] for v in hours.values()]
        rx = [v[1] for v in hours.values()]
        p('## Bus')
        p('')
        p(f'Gesendet im Mittel {sum(tx) / len(tx):.0f} Telegramme je Stunde, höchstens {max(tx)}. '
          f'Empfangen (Sensoren, Bedienung) im Mittel {sum(rx) / len(rx):.0f}, höchstens {max(rx)}. '
          f'Neustarts: {rec.boots}. Volllast-Signale (0 V an bipolarem Kanal): {len(rec.fullload)}.')
        p('')

    text = '\n'.join(L) + '\n'
    os.makedirs(out_dir, exist_ok=True)
    name = f"woche_{project.replace(' ', '_').replace('(', '').replace(')', '')}_{season}.md"
    path = os.path.join(out_dir, name)
    with open(path, 'w', encoding='utf-8') as f:
        f.write(text)
    return path, findings


def hours_total(rec):
    hours = collections.Counter()
    for (h, k), n in rec.tel_hour.items():
        if k == 'ko':
            hours[h] += n
    return sum(hours.values()) / len(hours) if hours else 0


def fmt_t(t):
    d = t // 86400000
    h = (t % 86400000) / 3600000
    return f"{WEEKDAY[d % 7]} {int(h):02d}:{int(h % 1 * 60):02d}"


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except AttributeError:
        pass
    ap = argparse.ArgumentParser(description='Wochenlauf des KWL-Simulators')
    ap.add_argument('--project', default='Aufbau B (Testplan)')
    ap.add_argument('--days', type=int, default=7)
    ap.add_argument('--season', default='Winter')
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--no-disturb', action='store_true')
    ap.add_argument('--out', default=os.path.join(HERE, 'build', 'reports'))
    a = ap.parse_args()
    print(f'Wochenlauf {a.project}, {a.days} Tage, {a.season}')
    path, findings = run(a.project, a.days, a.season, a.seed, not a.no_disturb, a.out)
    print(f'Bericht: {path}')
    print(f'{len(findings)} Befunde')
    for f in findings:
        print(' -', f.replace('**', ''))


if __name__ == '__main__':
    main()
