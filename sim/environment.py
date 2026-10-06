"""Umgebungssimulation: Aussenklima und Raumluft mit Zufallsmuster.

Erzeugt fuer jeden aktiven Raum Temperatur, relative Feuchte, CO2 und VOC und
sendet sie wie echte Sensoren auf die Objekte, die die ETS einblendet: bei
Aenderung um eine Schwelle oder spaetestens nach dem Sendeintervall. Alles
laeuft auf der Simulationszeit - bei 1x in Echtzeit, mit den Geschwindigkeits-
knoepfen entsprechend schneller.

Modell (bewusst einfach, aber physikalisch plausibel):

  Aussen     Tagesgang (Minimum gegen 5 Uhr, Maximum gegen 15 Uhr) je
             Jahreszeit, darueber eine langsame Zufallsschwankung
             (Ornstein-Uhlenbeck, Wetter). Feuchte gegenlaeufig zur Temperatur.
  Raum       Bilanz je Zeitschritt:
               CO2   Personen (18 l/h) gegen Luftwechsel mit Aussenluft
               Feuchte (absolut, g/kg)  Personen 50 g/h, Duschen, Kochen
                     gegen Luftwechsel mit der Aussenfeuchte
               Temperatur  Heizung zum Sollwert (Heizperiode), Gebaeudehuelle,
                     Personenwaerme, Luftwechsel mit Waermerueckgewinnung
               VOC   Personen und Ereignisse gegen Luftwechsel
  Luftwechsel  Infiltration 0,15 1/h plus - bei "Kopplung an Lueftung" - die
             Volumenstroeme, die die Firmware selbst meldet (Objekt Volumenstrom
             je Luefter). So schliesst sich der Regelkreis: CO2 steigt, die
             Fuehrung hebt die Stufe, der Luftwechsel senkt das CO2.
  Anwesenheit  je Raumprofil eine Wahrscheinlichkeit nach Tageszeit, alle
             10 min neu gewuerfelt; Ereignisse (Duschen, Kochen) zufaellig in
             ihren Zeitfenstern.

Alle Zufallszahlen kommen aus einem Generator mit Startwert (seed): derselbe
Startwert liefert denselben Ablauf.
"""

import math
import random

DAY_MS = 24 * 3600 * 1000

SEASONS = {
    'Winter':      dict(tmean=2.0,  tamp=4.0, rhmean=85.0, heating=True),
    'Übergang':    dict(tmean=10.0, tamp=6.0, rhmean=75.0, heating=True),
    'Sommer':      dict(tmean=21.0, tamp=7.0, rhmean=62.0, heating=False),
    'Hitzewelle':  dict(tmean=28.0, tamp=8.0, rhmean=45.0, heating=False),
}

# Anwesenheit: Liste (von_h, bis_h, Wahrscheinlichkeit je Person)
PROFILES = {
    'Wohnraum':     dict(volume=60, persons=2, setpoint=21.0,
                         presence=[(6.5, 8.0, 0.5), (12.0, 13.5, 0.3), (17.0, 23.0, 0.85)], events=[]),
    'Schlafzimmer': dict(volume=40, persons=2, setpoint=19.0,
                         presence=[(0.0, 6.5, 0.95), (22.5, 24.0, 0.9), (14.0, 15.0, 0.1)], events=[]),
    'Bad':          dict(volume=15, persons=1, setpoint=23.0,
                         presence=[(6.0, 8.0, 0.15), (20.0, 22.5, 0.1)],
                         events=[dict(name='Dusche', windows=[(6.0, 8.0, 0.35), (19.5, 22.5, 0.2)],
                                      minutes=(8, 15), moisture=1800, voc=150, persons=1)]),
    'Küche':        dict(volume=30, persons=2, setpoint=20.0,
                         presence=[(7.0, 8.0, 0.4), (11.5, 13.0, 0.4), (17.5, 19.5, 0.6)],
                         events=[dict(name='Kochen', windows=[(11.5, 13.0, 0.3), (17.5, 19.5, 0.45)],
                                      minutes=(25, 50), moisture=700, voc=900, persons=1)]),
    'Büro':         dict(volume=35, persons=2, setpoint=21.0,
                         presence=[(8.0, 12.0, 0.8), (13.0, 17.5, 0.75)], events=[]),
}
PROFILE_ORDER = ['Wohnraum', 'Schlafzimmer', 'Bad', 'Küche', 'Büro']

QUANTITIES = ('temp', 'hum', 'co2', 'voc', 'out', 'set')
STEP_MS = 10000            # Rechenschritt der Umgebung
INFILTRATION = 0.15        # 1/h
CO2_OUT = 420.0            # ppm
VOC_OUT = 60.0             # ppb
HRV_EFFICIENCY = 0.85      # Waermerueckgewinnung im Pendelbetrieb


# ------------------------------------------------------------------ Feuchte
def p_sat(t):
    """Saettigungsdampfdruck in hPa (Magnus, ueber Wasser)."""
    return 6.112 * math.exp(17.62 * t / (243.12 + t))


def x_from_rh(t, rh, p=1013.25):
    e = p_sat(t) * rh / 100.0
    return 622.0 * e / (p - e)


def rh_from_x(t, x, p=1013.25):
    e = x * p / (622.0 + x)
    return max(1.0, min(100.0, 100.0 * e / p_sat(t)))


class OU:
    """Ornstein-Uhlenbeck: Zufallsschwankung um 0 mit Rueckstellzeit tau (s)."""

    def __init__(self, rng, sigma, tau):
        self.rng, self.sigma, self.tau, self.v = rng, sigma, tau, 0.0

    def step(self, dt, strength):
        a = math.exp(-dt / self.tau)
        self.v = self.v * a + self.sigma * strength * math.sqrt(1 - a * a) * self.rng.gauss(0, 1)
        return self.v


class Environment:
    def __init__(self):
        self.config = dict(enabled=False, season='Übergang', startHour=6.0, strength=1.0,
                           coupling=True, events=True, cycleMin=5.0, seed=1, rooms={})
        self.reset()

    # ------------------------------------------------------------ Konfiguration
    def room_config(self, no):
        rooms = self.config['rooms']
        key = str(no)
        if key not in rooms:
            prof = PROFILE_ORDER[(no - 1) % len(PROFILE_ORDER)]
            base = PROFILES[prof]
            rooms[key] = dict(profile=prof, volume=base['volume'], persons=base['persons'],
                              setpoint=base['setpoint'], send={q: True for q in QUANTITIES})
        return rooms[key]

    def configure(self, patch):
        room = patch.pop('room', None)
        if room is not None:
            rc = self.room_config(int(room))
            if 'profile' in patch and patch['profile'] in PROFILES:
                base = PROFILES[patch['profile']]
                rc.update(profile=patch['profile'], volume=base['volume'], persons=base['persons'],
                          setpoint=base['setpoint'])
                patch.pop('profile')
            for q in QUANTITIES:
                if q in patch:
                    rc['send'][q] = bool(patch.pop(q))
            for k in ('volume', 'persons', 'setpoint'):
                if k in patch:
                    rc[k] = float(patch.pop(k))
            return
        reseed = 'seed' in patch and patch['seed'] != self.config['seed']
        for k, v in patch.items():
            if k in self.config and k != 'rooms':
                self.config[k] = type(self.config[k])(v) if not isinstance(self.config[k], bool) else bool(v)
        if reseed or 'season' in patch or 'startHour' in patch:
            self.reset(keep_time=True)

    def load(self, cfg):
        if cfg:
            rooms = cfg.get('rooms', {})
            self.config.update({k: v for k, v in cfg.items() if k != 'rooms'})
            self.config['rooms'] = rooms
        self.reset()

    # ------------------------------------------------------------ Zustand
    def reset(self, keep_time=False):
        self.rng = random.Random(int(self.config['seed']))
        self.ou_t = OU(self.rng, 2.0, 6 * 3600)
        self.ou_rh = OU(self.rng, 6.0, 4 * 3600)
        self.rooms = {}
        self.windows = {}         # Raum -> offen bis (ms), gesetzt vom Bewohner
        self.out = None
        self.next = 0 if not keep_time else getattr(self, 'next', 0)
        self.sent = {}            # nr -> (value, t)
        self.history = []         # [t, To, RHo, {no: [T, RH, CO2, VOC, persons, Q]}]
        self.last_hist = -10 ** 12
        self.status = {}

    def tod_hours(self, t_ms):
        return (self.config['startHour'] + t_ms / 3600000.0) % 24.0

    def day(self, t_ms):
        return int((self.config['startHour'] + t_ms / 3600000.0) // 24)

    def _outdoor(self, t_ms, dt):
        s = SEASONS.get(self.config['season'], SEASONS['Übergang'])
        h = self.tod_hours(t_ms)
        diurnal = -math.cos(2 * math.pi * (h - 3.0) / 24.0)          # -1 um 3 Uhr ... +1 um 15 Uhr
        st = self.config['strength']
        to = s['tmean'] + s['tamp'] * diurnal + self.ou_t.step(dt, st)
        rho = s['rhmean'] - 12.0 * diurnal + self.ou_rh.step(dt, st)
        rho = max(25.0, min(99.0, rho))
        return to, rho

    def _room_state(self, no, to, rho):
        if no not in self.rooms:
            rc = self.room_config(no)
            sp = rc['setpoint']
            t0 = sp if SEASONS[self.config['season']]['heating'] else max(sp, to + 2)
            self.rooms[no] = dict(T=t0, x=min(x_from_rh(t0, 50.0), x_from_rh(to, rho) + 2.0),
                                  C=CO2_OUT + 150, V=VOC_OUT * 2, persons=0, event=None,
                                  event_until=0, next_presence=0, ou_T=OU(self.rng, 0.15, 1800))
        return self.rooms[no]

    def _presence(self, rs, rc, prof, t_ms):
        if t_ms < rs['next_presence']:
            return
        rs['next_presence'] = t_ms + 10 * 60 * 1000
        h = self.tod_hours(t_ms)
        p = 0.0
        for a, b, q in prof['presence']:
            if a <= h < b:
                p = max(p, q)
        rs['persons'] = sum(1 for _ in range(int(rc['persons'])) if self.rng.random() < p)
        # Ereignisse
        if self.config['events'] and rs['event'] is None:
            for ev in prof['events']:
                for a, b, q in ev['windows']:
                    if a <= h < b:
                        # q = Wahrscheinlichkeit je Fenster -> je 10-min-Wuerfel
                        per = 1 - (1 - q) ** (1.0 / max(1.0, (b - a) * 6))
                        if self.rng.random() < per:
                            mins = self.rng.uniform(*ev['minutes'])
                            rs['event'] = ev
                            rs['event_until'] = t_ms + mins * 60000
                        break

    # ------------------------------------------------------------ Schritt
    def step(self, t_ms, active_rooms, flows):
        """Einen Rechenschritt bis t_ms. active_rooms: Liste Raumnummern.
        flows: Raum -> (Volumenstrom m3/h, WRG aktiv). Liefert den Zustand."""
        dt = STEP_MS / 1000.0
        to, rho = self._outdoor(t_ms, dt)
        xo = x_from_rh(to, rho)
        self.out = dict(T=to, RH=rho, x=xo)
        season = SEASONS[self.config['season']]
        st = self.config['strength']
        status = {}
        for no in active_rooms:
            rc = self.room_config(no)
            prof = PROFILES.get(rc['profile'], PROFILES['Wohnraum'])
            rs = self._room_state(no, to, rho)
            self._presence(rs, rc, prof, t_ms)
            if rs['event'] is not None and t_ms >= rs['event_until']:
                rs['event'] = None
            ev = rs['event']
            persons = max(rs['persons'], ev['persons'] if ev else 0)
            vol = max(5.0, rc['volume'])

            q, hrv = flows.get(no, (0.0, False)) if self.config['coupling'] else (0.0, False)
            n_mech = q / vol                      # 1/h
            # Offenes Fenster: Stosslueftung mit rund 4 1/h, ohne Waermerueckgewinnung
            n_win = 4.0 if self.windows.get(no, 0) > t_ms else 0.0
            n_tot = INFILTRATION + n_mech + n_win
            dth = dt / 3600.0

            # CO2 (ppm)
            gen_c = persons * 18000.0 / vol      # ppm/h
            rs['C'] += (gen_c - n_tot * (rs['C'] - CO2_OUT)) * dth
            # VOC (ppb)
            gen_v = persons * 120.0 / vol * 10 + (ev['voc'] * 30.0 / vol if ev else 0.0)
            rs['V'] += (gen_v - n_tot * (rs['V'] - VOC_OUT)) * dth
            # Feuchte (g/kg): Luftmasse ~1,2 kg/m3
            gen_x = (persons * 50.0 + (ev['moisture'] if ev else 0.0)) / (1.2 * vol)
            rs['x'] += (gen_x - n_tot * (rs['x'] - xo)) * dth
            # Temperatur (K): traege Raummasse, Heizung, Huelle, Luftwechsel
            eta = HRV_EFFICIENCY if hrv else 0.0
            heat = 0.0
            if season['heating'] and rs['T'] < rc['setpoint']:
                heat = min(4.0, (rc['setpoint'] - rs['T']) * 4.0)          # K/h, Thermostat
            gains = persons * 0.25 * 50.0 / vol + (0.6 if ev and ev['name'] == 'Kochen' else 0.0)
            envelope = (to - rs['T']) / 40.0
            # 0,15: die Luft ist nur ein kleiner Teil der Waermekapazitaet des Raums
            vent = (INFILTRATION + n_win + n_mech * (1 - eta)) * (to - rs['T']) * 0.15
            rs['T'] += (heat + gains + envelope + vent) * dth + rs['ou_T'].step(dt, st) * 0.02
            # Kondensation: ueber 100 % rF faellt Wasser aus
            rs['x'] = min(rs['x'], x_from_rh(rs['T'], 100.0))
            rs['C'] = max(CO2_OUT, rs['C'])
            rs['V'] = max(VOC_OUT * 0.5, rs['V'])

            noise = lambda s: self.rng.gauss(0, s * st)
            status[no] = dict(
                T=rs['T'] + noise(0.05), RH=rh_from_x(rs['T'], rs['x']) + noise(0.3),
                x=rs['x'], CO2=rs['C'] + noise(8), VOC=max(0.0, rs['V'] + noise(5)),
                persons=persons, event=ev['name'] if ev else '', Q=q, n=n_tot, hrv=hrv,
                setpoint=rc['setpoint'], profile=rc['profile'], window=n_win > 0)
        self.status = status
        if t_ms - self.last_hist >= 60000:
            self.last_hist = t_ms
            self.history.append([t_ms, round(to, 2), round(rho, 1),
                                 {no: [round(s['T'], 2), round(s['RH'], 1), round(s['CO2']),
                                       round(s['VOC']), s['persons'], round(s['Q'], 1)]
                                  for no, s in status.items()}])
            if len(self.history) > 20000:
                del self.history[:5000]
        return status

    # ------------------------------------------------------------ Telegramme
    def telegrams(self, t_ms, kos_by_room):
        """Was die Sensoren jetzt senden. kos_by_room: Raum -> {Groesse: (nr, dpt)}.
        Sendet bei Aenderung um die Schwelle oder nach dem Sendeintervall."""
        cycle = max(10.0, self.config['cycleMin'] * 60.0) * 1000
        out = []
        for no, s in self.status.items():
            rc = self.room_config(no)
            kos = kos_by_room.get(no, {})
            vals = []
            if rc['send'].get('temp'):
                vals.append(('TempIn', round(s['T'], 1), 0.2))
            if rc['send'].get('hum'):
                vals.append(('HumIn', round(s['RH'], 1), 1.0))
            if rc['send'].get('co2'):
                vals.append(('CO2', round(s['CO2']), 25))
            if rc['send'].get('voc'):
                vals.append(('VOC', round(s['VOC']), 20))
            if rc['send'].get('set'):
                vals.append(('TempSet', round(s['setpoint'], 1), 0.1))
            if rc['send'].get('out') and self.out:
                vals.append(('TempOut', round(self.out['T'], 1), 0.3))
                vals.append(('HumOut', round(self.out['RH'], 1), 2.0))
            for name, value, delta in vals:
                target = kos.get(name)
                if not target:
                    continue
                nr, dpt = target
                if name == 'VOC' and dpt != '9.8':
                    # Index-Formate: VOC-Index um 100 = ueblich (Annahme, wie
                    # Sensirion SGP40), 5.010 auf 255 begrenzt.
                    value = round(max(1, min(500, value / 3.0)))
                    if dpt == '5.10':
                        value = min(255, value)
                    delta = 5
                last = self.sent.get(nr)
                if last is None or abs(value - last[0]) >= delta or t_ms - last[1] >= cycle:
                    self.sent[nr] = (value, t_ms)
                    out.append((nr, dpt, value))
        return out
