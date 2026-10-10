"""Selbsttest des Simulators: ein paar Abläufe aus dem Testplan, ohne Oberfläche.

Prüft, dass der Kern den echten Firmware-Code ausführt und die Ausgänge so
liefert, wie Testplan und Applikationsbeschreibung es erwarten. Läuft nach dem
Bauen:  python sim/selftest.py
"""

import sys

from kwlcore import Core, ParamImage, load_meta

FAILS = []


def check(label, cond, detail=''):
    print(('  ok    ' if cond else '  FEHLER ') + label + (f'  [{detail}]' if detail else ''))
    if not cond:
        FAILS.append(label)


def aufbau_b(meta):
    """Testplan, Vorbereitung: Aufbau B (beide Lüfter in Verbund 1)."""
    img = ParamImage(meta)
    for n, v in {
        'FAN_Hardware': 1, 'BASE_Info1LedFunc': 110,
        'ROOM_r1Active': 1, 'ROOM_r2Active': 1,
        'FAN_Grp1Active': 1,
        'FAN_f1Active': 1, 'FAN_f1Type': 0, 'FAN_f1Channel': 1, 'FAN_f1Room': 1,
        'FAN_f1Group': 1, 'FAN_f1Phase': 0,
        'FAN_f2Active': 1, 'FAN_f2Type': 0, 'FAN_f2Channel': 2, 'FAN_f2Room': 2,
        'FAN_f2Group': 1, 'FAN_f2Phase': 1,
    }.items():
        img.set(n, v)
    return img


def project_image(meta, name):
    import json, os
    with open(os.path.join(os.path.dirname(__file__), 'projects', name + '.json'), encoding='utf-8') as f:
        img = ParamImage(meta)
        img.load_values(json.load(f)['values'])
    return img


def ego_summer(meta):
    """Befund A: ego ohne WRG im Takt darf seinen Status nicht in jeder Schleife senden."""
    print('9. ego im Sommerzyklus (Befund A, Telegrammsturm)')
    sent = []
    core = Core(on_event=lambda ev: sent.append(ev) if ev.get('ev') == 'ko' else None)
    core.cmd('load ' + project_image(meta, 'Bad mit ego + e2 60-Paar').hex())
    core.cmd('v12 1')
    core.cmd('bus 1')
    core.cmd('run 2000')
    core.cmd('ko 30 1.1 1')          # Raum 1 (Bad): Sommer -> langer Zyklus, keine WRG
    core.cmd('run 1000')
    sent.clear()
    core.cmd('run 600000')           # 10 min
    ego = [e for e in sent if 340 <= e['nr'] < 364]
    check('Lüfter 1 (ego) sendet in 10 min höchstens 20 Telegramme', len(ego) <= 20, f'{len(ego)} Telegramme')
    core.close()


def ego_boost(meta):
    """Befund C: Abluftstoss des ego nur ueber die Totzeit bei 5,00 V."""
    print('10. Abluftstoß des ego aus dem Pendeln (Befund C, Totzeit je Kanal)')
    warns = []
    core = Core(on_event=lambda ev: warns.append(ev) if ev.get('ev') == 'warn' else None)
    core.cmd('load ' + project_image(meta, 'Bad mit ego + e2 60-Paar').hex())
    core.cmd('v12 1')
    core.cmd('bus 1')
    core.cmd('run 5000')
    core.cmd('ko 26 5.100 1')         # Bad: Handstufe 1, der ego pendelt
    for _ in range(3):                 # ueber mehrere Pendelwechsel
        core.cmd('run 40000')
        core.cmd('ko 32 1.3 1')        # Abluftanforderung (10 s Vorlauf)
        core.cmd('run 20000')
        core.cmd('ko 32 1.3 0')
        core.cmd('run 70000')          # Nachlauf 1 min, zurueck ins Pendeln
    i5 = [w for w in warns if w['inv'] == 'Invariante 5']
    check('kein Richtungswechsel ohne Totzeit am ego', not i5, [w['text'] for w in i5][:2])
    core.cmd('ko 32 1.3 1')
    core.cmd('run 15000')
    fan = core.state()['sim']['fans'][0]
    check('ego erreicht Stufe 4 (Abluftstoß)', fan['stage'] == 4, fan['stage'])
    core.close()


def polarity(meta):
    """Polaritaet je Luefter (2026-10-10): Standard ueber 5 V Zuluft, umkehrbar je Luefter."""
    print('11. Polarität je Lüfter')
    img = aufbau_b(meta)
    for n, v in {'FAN_Grp2Active': 1, 'FAN_f2Group': 2,   # je ein eigener Verbund
                 'FAN_f1Polarity': 0, 'FAN_f2Polarity': 1}.items():
        img.set(n, v)
    core = Core()
    core.cmd('load ' + img.hex())
    core.cmd('v12 1')
    core.cmd('bus 1')
    core.cmd('run 5000')
    for ko_stage, ko_dir in ((26, 31), (66, 71)):        # Raum 1 und 2: Stufe 2, nur Zuluft
        core.cmd(f'ko {ko_stage} 5.100 2')
        core.cmd(f'ko {ko_dir} 5.010 2')
    core.cmd('run 15000')             # Rampe ab, Totzeit, Rampe auf
    st = core.state()['sim']
    f1, f2 = st['fans'][0], st['fans'][1]
    v1, v2 = st['dac'][0]['volt'], st['dac'][1]['volt']
    check('Lüfter 1, Standard: Zuluft über 5 V', f1['dir'] == 'Zuluft' and abs(v1 - 6.91) < 0.05,
          (f1['dir'], v1, f1['below5vSupply']))
    check('Lüfter 2, umgekehrt: Zuluft unter 5 V', f2['dir'] == 'Zuluft' and abs(v2 - 3.21) < 0.05,
          (f2['dir'], v2, f2['below5vSupply']))
    core.close()


def without_group(meta):
    """Seit 2026-10-10 braucht jeder Luefter einen aktiven Verbund."""
    print('12. Lüfter ohne aktiven Verbund')
    img = aufbau_b(meta)
    img.set('FAN_f2Group', 3)                              # Verbund 3 ist nicht aktiv
    core = Core()
    core.cmd('load ' + img.hex())
    core.cmd('v12 1')
    core.cmd('bus 1')
    core.cmd('run 5000')
    core.cmd('ko 66 5.100 3')                              # Raum 2 will Stufe 3
    core.cmd('run 5000')
    st = core.state()['sim']
    check('Lüfter 2: Fehlercode 3', st['fans'][1]['err'] == 3, st['fans'][1]['err'])
    check('Lüfter 2: S2 auf 5,00 V', abs(st['dac'][1]['volt'] - 5.0) < 0.02, st['dac'][1])
    check('Lüfter 1 nicht betroffen (Verbund 1)', st['fans'][0]['err'] != 3, st['fans'][0]['err'])
    core.close()


def dac(core):
    return [round(c['volt'], 2) for c in core.state()['sim']['dac']]


def main():
    try:
        sys.stdout.reconfigure(encoding='utf-8')
    except AttributeError:
        pass
    meta = load_meta()
    core = Core()
    warns = []
    sent = []

    def collect(ev):
        if ev.get('ev') == 'warn':
            warns.append(ev)
        elif ev.get('ev') == 'ko':
            sent.append(ev)
    core.on_event = collect

    print('1. Neues Board, KNX-Bus vor 12 V (Erstinbetriebnahme)')
    core.cmd('bus 1')
    core.cmd('run 1000')
    core.cmd('v12 1')
    core.cmd('run 20')
    v = dac(core)
    check('direkt nach 12 V ein: Werkswert 0 V', v[0] < 0.1, v)
    core.cmd('run 400')
    v = dac(core)
    check('nach 0,4 s: 5,00 V an S1..S4', all(abs(x - 5.0) < 0.02 for x in v), v)

    print('2. Programmieren (ETS-Download) mit Aufbau B')
    core.cmd('pimg ' + aufbau_b(meta).hex())
    core.cmd('ets 3000')
    core.cmd('run 2000')
    st = core.state()
    check('nach dem Download parametriert', st['configured'])
    check('zwei Lüfter aktiv', len(st['sim']['fans']) == 2, len(st['sim']['fans']))

    print('3. Handstufe 2 in Raum 1, Komfort in beiden Räumen (T4)')
    core.cmd('ko 20 20.102 1')
    core.cmd('ko 60 20.102 1')
    core.cmd('ko 26 5.100 2')
    core.cmd('run 3000')
    v = dac(core)
    check('S1 und S2 in verschiedenen Hälften', (v[0] - 5) * (v[1] - 5) < 0, v)
    rooms = core.state()['sim']['rooms']
    check('Betriebsart Komfort über Objekt 20 kommt an (T3)', rooms[0]['mode'] == 'Komfort',
          f"Raum 1: {rooms[0]['mode']} - Befund: die KNX-Bibliothek dekodiert DPT 20.102 nicht")
    st47 = [e['val'] for e in sent if e['nr'] == 47]
    st48 = [e['val'] for e in sent if e['nr'] == 48]
    check('Objekt 47 (Betriebsart Status) meldet 1 = Komfort', bool(st47) and st47[-1] == 1, st47[-3:])
    check('Objekt 48 (HVAC-Status) meldet Bit 0 = Komfort', bool(st48) and st48[-1] == 1, st48[-3:])
    fans = core.state()['sim']['fans']
    check('beide Lüfter Stufe 2', [f['stage'] for f in fans] == [2, 2], [f['stage'] for f in fans])
    dir348 = [e['val'] for e in sent if e['nr'] == 348]
    check('Objekt 348 Richtung Status: 1 = Zuluft (Befund I)',
          bool(dir348) and dir348[-1] == (fans[0]['dir'] == 'Zuluft'), (fans[0]['dir'], dir348[-2:]))

    print('4. Pendeln über die Totzeit (70 s Zyklus)')
    side0 = v[0] > 5
    full0 = abs(v[0] - 5.0)
    # Seit 2026-10-10: Rampe 5 s (ETS-Vorgabe) vor und nach der Totzeit.
    waited = 0
    while waited < 80000 and core.state()['sim']['groups'][0]['ramp'] >= 0.999:
        core.cmd('run 100')
        waited += 100
    core.cmd('run 2500')              # Mitte der Rampe nach unten
    g = core.state()['sim']['groups'][0]
    x = dac(core)
    check('Rampe ab: halber Ausschlag, alte Seite',
          abs(g['ramp'] - 0.5) < 0.05 and (x[0] > 5) == side0 and 0.3 * full0 < abs(x[0] - 5) < 0.7 * full0,
          (g['ramp'], x))
    waited = 0
    while waited < 10000 and not core.state()['sim']['groups'][0]['inDeadTime']:
        core.cmd('run 100')
        waited += 100
    check('Rampe dauert 5 s', 2300 <= waited <= 2700, f'{2500 + waited} ms bis 5,00 V')
    x = dac(core)
    check('Totzeit erreicht, beide Kanäle 5,00 V', abs(x[0] - 5.0) < 0.02 and abs(x[1] - 5.0) < 0.02, x)
    dead = 0
    while dead < 10000 and core.state()['sim']['groups'][0]['inDeadTime']:
        core.cmd('run 100')
        dead += 100
    check('Totzeit dauert 2 s', 1900 <= dead <= 2100, f'{dead} ms')
    core.cmd('run 2500')              # Mitte der Rampe nach oben
    x = dac(core)
    check('Rampe auf: neue Seite, halber Ausschlag',
          (x[0] > 5) != side0 and 0.3 * full0 < abs(x[0] - 5) < 0.7 * full0, x)
    core.cmd('run 2600')
    v = dac(core)
    # Stufe 2 liegt nicht symmetrisch um 5 V: 6,91 V Zuluft, 3,21 V Abluft.
    check('danach getauscht, volle Stufe', (v[0] > 5) != side0 and min(abs(v[0] - 6.91), abs(v[0] - 3.21)) < 0.02, v)
    fans = core.state()['sim']['fans']
    check('Status in der Totzeit = laufende Stufe (Befund F)', fans[0]['stage'] == 2, fans[0]['stage'])
    sent.clear()
    core.cmd('run 140000')            # zwei Pendelwechsel
    status = [e for e in sent if 346 <= e['nr'] <= 351]
    check('Lüfter 1: höchstens 3 Statustelegramme je Pendelwechsel (Befund F)', len(status) <= 6,
          f'{len(status)} in 140 s')

    print('5. Sperre Raum 1 (T4 Sperre im Verbund)')
    core.cmd('ko 26 5.100 0')
    core.cmd('ko 66 5.100 2')
    core.cmd('ko 33 1.3 1')
    core.cmd('run 3000')
    fans = core.state()['sim']['fans']
    check('Lüfter 1 steht (Stufe 0)', fans[0]['stage'] == 0, fans[0]['stage'])
    check('Lüfter 2 läuft weiter (Stufe 2)', fans[1]['stage'] == 2, fans[1]['stage'])
    core.cmd('ko 33 1.3 0')

    print('6. Abluftanforderung Raum 1 gegen Standby-Deckel von Raum 2 (T10 2a)')
    core.cmd('ko 66 5.100 0')
    core.cmd('ko 60 20.102 2')
    core.cmd('ko 32 1.3 1')
    core.cmd('run 13000')             # Rampe ab 5 s, Totzeit 2 s, Rampe auf 5 s
    fans = core.state()['sim']['fans']
    check('Lüfter 1 Abluft Stufe 4', fans[0]['stage'] == 4 and fans[0]['dir'] == 'Abluft',
          (fans[0]['stage'], fans[0]['dir']))
    check('Lüfter 2 Zuluft, Verbundstufe', fans[1]['dir'] == 'Zuluft', (fans[1]['stage'], fans[1]['dir']))
    core.cmd('ko 32 1.3 0')

    print('6b. Feuchtevergleich: Code 7 nur mit Feuchteanforderung (Befund E)')
    core.cmd('ko 35 9.1 18')          # innen 18 °C
    core.cmd('ko 37 9.1 20')          # außen 20 °C, 80 % - feuchter als innen
    core.cmd('ko 36 9.7 80')
    core.cmd('ko 34 9.7 40')          # innen 40 %: keine Anforderung
    core.cmd('run 3000')
    r1 = core.state()['sim']['rooms'][0]
    blk = [e['val'] for e in sent if e['nr'] == 51]
    check('Objekt 51 meldet den Vergleich (sperrt)', bool(blk) and blk[-1] is True, blk[-2:])
    check('ohne Anforderung kein Code 7', r1['err'] != 7, r1['err'])
    core.cmd('ko 34 9.7 60')          # innen 60 %: über GW1 (50 %)
    core.cmd('run 3000')
    r1 = core.state()['sim']['rooms'][0]
    check('mit Anforderung Code 7', r1['err'] == 7, r1['err'])

    print('7. 12-V-Ausfall im Betrieb und Wiederkehr')
    core.cmd('ko 26 5.100 3')
    core.cmd('run 2000')
    core.cmd('v12 0')
    core.cmd('run 500')
    st = core.state()
    check('Fehlercode 4 an Lüfter 1', st['sim']['fans'][0]['err'] == 4, st['sim']['fans'][0]['err'])
    n_i5 = sum(1 for w in warns if w['inv'] == 'Invariante 5')
    core.cmd('v12 1')
    core.cmd('run 600')
    check('nach 12-V-Wiederkehr kein Richtungswechsel ohne Totzeit',
          sum(1 for w in warns if w['inv'] == 'Invariante 5') == n_i5,
          'Befund: 0 V (Werkswert) und danach sofort Stufe 4 in Gegenrichtung')
    core.cmd('run 2000')
    st = core.state()
    check('Fehler weg nach Wiederkehr', st['sim']['module']['error'] == 0, st['sim']['module'])
    # Nicht "Spannung > 0,5 V": seit 2026-10-10 ist unter 5 V Abluft, und Stufe 4
    # Abluft des e²60 liegt bei 0,01 V. Der Werkswert ist Code 0 ohne Bereich.
    d0 = st['sim']['dac'][0]
    check('Ausgang neu geschrieben, nicht Werkswert', d0['wire'] != 0 and d0['range'] == 0x11, d0)
    f0 = st['sim']['fans'][0]
    check('Spannung passt zur Richtung (Polarität Standard)',
          f0['stage'] == 0 or (d0['volt'] > 5.0) == (f0['dir'] == 'Zuluft'), (f0['stage'], f0['dir'], d0['volt']))

    print('8. KNX-Bus weg, 12 V bleiben')
    core.cmd('run 3000')
    core.cmd('bus 0')
    core.cmd('run 1000')
    v = dac(core)
    check('S1/S2 auf 5,00 V gehalten', abs(v[0] - 5) < 0.02 and abs(v[1] - 5) < 0.02, v)

    core.close()
    ego_summer(meta)
    ego_boost(meta)
    polarity(meta)
    without_group(meta)

    print('Invariantenwächter:')
    for w in warns:
        print('  ', w['inv'], '-', w['text'])
    if not warns:
        print('   keine Meldung')

    print()
    print('ALLES OK' if not FAILS else f'{len(FAILS)} FEHLER: ' + '; '.join(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
