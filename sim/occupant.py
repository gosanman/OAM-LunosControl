"""Bewohner-Simulation: ein Mensch, der die Lueftung bedient.

Dritte Ebene ueber Firmware und Umgebung. Der Bewohner sieht, was ein Mensch
sieht (Anwesenheit, Duschen, stickige Luft aus der Umgebungssimulation, die
Tageszeit) und bedient die Anlage ueber dieselben Objekte, die auch ein Taster
oder eine Visualisierung benutzt. Jede Aktion wird mit Begruendung protokolliert.

Verhalten (jedes einzeln abschaltbar):

  tagesablauf  morgens Nacht aus und Komfort, werktags tagsueber Standby
               (niemand da), abends wieder Komfort, zur Schlafenszeit Nacht ein
  handstufe    bei stickiger Luft oder hoher Feuchte im besetzten Raum die
               Handstufe 3 - die Firmware nimmt sie nach der Laufzeit zurueck
  fenster      bei sehr stickiger Luft das Fenster auf: Sperre 1 (Fensterkontakt),
               in der Umgebung 4 1/h Luftwechsel, nach 10-20 min wieder zu
  badluefter   beim Duschen die Abluftanforderung, danach aus - manchmal
               vergessen (dann laeuft der Nachlauf erst spaeter an)
  taste        selten die Funktionstaste am Geraet (Stossluefen ein/aus)
  sommer       im Sommer das Objekt "Sommer" auf 1 (langer Zyklus)

Alle Zufallszahlen aus einem eigenen Generator mit Startwert.
"""

import random

DECIDE_MS = 60000          # der Mensch schaut einmal je Minute hin

BEHAVIOURS = {
    'tagesablauf': 'Tagesablauf (Betriebsart, Nacht)',
    'handstufe': 'Handstufe bei stickiger Luft',
    'fenster': 'Fenster öffnen (Sperre)',
    'badluefter': 'Badlüftung beim Duschen',
    'taste': 'Funktionstaste am Gerät',
    'sommer': 'Objekt „Sommer“ je Jahreszeit',
}


class Occupant:
    def __init__(self):
        self.config = dict(enabled=False, activity=1.0, seed=7,
                           behave={k: True for k in BEHAVIOURS})
        self.reset()

    def reset(self):
        self.rng = random.Random(int(self.config['seed']))
        self.next = 0
        self.rooms = {}            # Raum -> Zustand
        self.plan_day = None
        self.plan = {}
        self.log = []              # [t, Raum, Text]
        self.summer_sent = None
        self.boost_until = 0

    def load(self, cfg):
        if cfg:
            beh = dict(self.config['behave'])
            beh.update(cfg.get('behave', {}))
            self.config.update({k: v for k, v in cfg.items() if k != 'behave'})
            self.config['behave'] = beh
        self.reset()

    def configure(self, patch):
        if 'behave' in patch:
            self.config['behave'].update({k: bool(v) for k, v in patch.pop('behave').items()})
        for k, v in patch.items():
            if k in ('enabled',):
                self.config[k] = bool(v)
            elif k == 'activity':
                self.config[k] = float(v)
            elif k == 'seed':
                self.config[k] = int(v)
                self.reset()

    # ------------------------------------------------------------ Tagesplan
    def _new_day(self, day):
        """Je Tag leicht verschobene Zeiten - Menschen sind nicht puenktlich."""
        r = self.rng
        weekend = day % 7 >= 5
        self.plan = dict(
            wake=(7.5 if weekend else 6.25) + r.uniform(-0.5, 0.75),
            leave=None if weekend or r.random() < 0.1 else 7.75 + r.uniform(0, 0.75),
            back=17.0 + r.uniform(-0.75, 1.5),
            bed=22.5 + r.uniform(-0.5, 1.0),
        )
        self.plan_day = day

    def _phase(self, h):
        p = self.plan
        if h < p['wake'] or h >= p['bed']:
            return 'nacht'
        if p['leave'] is not None and p['leave'] <= h < p['back']:
            return 'weg'
        return 'da'

    # ------------------------------------------------------------ Schritt
    def step(self, t_ms, env, kos_by_room, button_ok):
        """Liefert Aktionen: (nr, dpt, value, raum, text) oder ('btn', kind, raum, text)."""
        if not self.config['enabled'] or t_ms < self.next:
            return []
        self.next = t_ms + DECIDE_MS
        beh = self.config['behave']
        act = self.config['activity']
        r = self.rng
        h = env.tod_hours(t_ms)
        day = env.day(t_ms)
        if self.plan_day != day:
            self._new_day(day)
        phase = self._phase(h)
        out = []

        def send(no, name, dpt, value, text):
            target = kos_by_room.get(no, {}).get(name)
            if target:
                out.append((target[0], dpt, value, no, text))

        # Sommer-Objekt einmal je Jahreszeitwechsel
        if beh['sommer']:
            summer = env.config['season'] in ('Sommer', 'Hitzewelle')
            if self.summer_sent != summer:
                self.summer_sent = summer
                for no in kos_by_room:
                    send(no, 'Summer', '1.1', int(summer), 'Sommer ein' if summer else 'Sommer aus')

        for no, status in env.status.items():
            rs = self.rooms.setdefault(no, dict(mode=None, night=None, window_until=0,
                                                exhaust=False, hand_at=-10 ** 12))
            present = status['persons'] > 0
            profile = status['profile']

            # --- Tagesablauf ----------------------------------------------------
            if beh['tagesablauf']:
                want_night = phase == 'nacht'
                if rs['night'] != want_night:
                    rs['night'] = want_night
                    send(no, 'Night', '1.3', int(want_night), 'Schlafenszeit: Nacht ein' if want_night else 'Aufgestanden: Nacht aus')
                want_mode = 2 if phase == 'weg' else 1
                if phase != 'nacht' and rs['mode'] != want_mode:
                    rs['mode'] = want_mode
                    send(no, 'Mode', '20.102', want_mode,
                         'Aus dem Haus: Betriebsart Standby' if want_mode == 2 else 'Zuhause: Betriebsart Komfort')

            # --- Fenster --------------------------------------------------------
            if beh['fenster']:
                if rs['window_until'] and t_ms >= rs['window_until']:
                    rs['window_until'] = 0
                    env.windows.pop(no, None)
                    send(no, 'Lock', '1.3', 0, 'Fenster zu')
                elif not rs['window_until'] and present and status['CO2'] > 1600 and r.random() < 0.15 * act:
                    minutes = r.uniform(10, 20)
                    rs['window_until'] = t_ms + minutes * 60000
                    env.windows[no] = rs['window_until']
                    send(no, 'Lock', '1.3', 1, f'Zu stickig ({status["CO2"]:.0f} ppm): Fenster auf für {minutes:.0f} min')

            # --- Handstufe ------------------------------------------------------
            if beh['handstufe'] and present and not rs['window_until'] and t_ms - rs['hand_at'] > 90 * 60000:
                stuffy = status['CO2'] > 1300 or status['RH'] > 70
                if (stuffy and r.random() < 0.08 * act) or r.random() < 0.002 * act:
                    rs['hand_at'] = t_ms
                    why = f'CO₂ {status["CO2"]:.0f} ppm, rF {status["RH"]:.0f} %' if stuffy else 'einfach so'
                    send(no, 'StageSet', '5.100', 3, f'Handstufe 3 ({why})')

            # --- Badlüftung beim Duschen ------------------------------------------
            if beh['badluefter'] and 'ExhaustReq' in kos_by_room.get(no, {}):
                showering = status['event'] in ('Dusche', 'Kochen')
                if showering and not rs['exhaust']:
                    if r.random() < 0.85:
                        rs['exhaust'] = True
                        send(no, 'ExhaustReq', '1.3', 1, f'{status["event"]}: Abluft an')
                    else:
                        rs['exhaust'] = 'vergessen'
                elif not showering and rs['exhaust']:
                    if rs['exhaust'] is True:
                        send(no, 'ExhaustReq', '1.3', 0, 'fertig: Abluft aus')
                    rs['exhaust'] = False

        # --- Funktionstaste -----------------------------------------------------
        if beh['taste'] and button_ok and phase == 'da':
            if self.boost_until and t_ms >= self.boost_until:
                self.boost_until = 0
                out.append(('btn', 'short', 0, 'Taste: Stoßlüften wieder aus'))
            elif not self.boost_until and r.random() < 0.0015 * act:
                self.boost_until = t_ms + r.uniform(10, 25) * 60000
                out.append(('btn', 'short', 0, 'Taste: Stoßlüften ein'))

        for a in out:
            self.log.append([t_ms, a[-2], a[-1]])
        if len(self.log) > 5000:
            del self.log[:1000]
        return out
