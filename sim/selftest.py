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
    core.on_event = lambda ev: warns.append(ev) if ev.get('ev') == 'warn' else None

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
    fans = core.state()['sim']['fans']
    check('beide Lüfter Stufe 2', [f['stage'] for f in fans] == [2, 2], [f['stage'] for f in fans])

    print('4. Pendeln über die Totzeit (70 s Zyklus)')
    side0 = v[0] > 5
    waited = 0
    while waited < 80000 and not core.state()['sim']['groups'][0]['inDeadTime']:
        core.cmd('run 100')
        waited += 100
    x = dac(core)
    check('Totzeit erreicht, beide Kanäle 5,00 V', abs(x[0] - 5.0) < 0.02 and abs(x[1] - 5.0) < 0.02, x)
    dead = 0
    while dead < 10000 and core.state()['sim']['groups'][0]['inDeadTime']:
        core.cmd('run 100')
        dead += 100
    check('Totzeit dauert 2 s', 1900 <= dead <= 2100, f'{dead} ms')
    v = dac(core)
    check('danach getauscht', (v[0] > 5) != side0, v)

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
    core.cmd('run 4000')
    fans = core.state()['sim']['fans']
    check('Lüfter 1 Abluft Stufe 4', fans[0]['stage'] == 4 and fans[0]['dir'] == 'Abluft',
          (fans[0]['stage'], fans[0]['dir']))
    check('Lüfter 2 Zuluft, Verbundstufe', fans[1]['dir'] == 'Zuluft', (fans[1]['stage'], fans[1]['dir']))
    core.cmd('ko 32 1.3 0')

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
    check('Ausgang nicht auf 0 V', st['sim']['dac'][0]['volt'] > 0.5, st['sim']['dac'][0])

    print('8. KNX-Bus weg, 12 V bleiben')
    core.cmd('run 3000')
    core.cmd('bus 0')
    core.cmd('run 1000')
    v = dac(core)
    check('S1/S2 auf 5,00 V gehalten', abs(v[0] - 5) < 0.02 and abs(v[1] - 5) < 0.02, v)

    print('Invariantenwächter:')
    for w in warns:
        print('  ', w['inv'], '-', w['text'])
    if not warns:
        print('   keine Meldung')

    core.close()
    print()
    print('ALLES OK' if not FAILS else f'{len(FAILS)} FEHLER: ' + '; '.join(FAILS))
    return 1 if FAILS else 0


if __name__ == '__main__':
    sys.exit(main())
