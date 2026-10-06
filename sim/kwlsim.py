"""KWL-Simulator - Python-App mit grafischer Oberflaeche im Browser.

Startet den Simulatorkern (build/kwlsim.exe, der UNVERAENDERTE Firmware-Code
des Lueftungsmoduls), treibt die Simulationszeit und stellt die Oberflaeche
unter http://127.0.0.1:8765 bereit. Nur Python-Standardbibliothek.

    python sim/kwlsim.py [--port 8765] [--no-browser] [--project "Aufbau B"]

Was die App tut:
  - Parameter wie in der ETS setzen und "programmieren" (ETS-Download mit
    Neustart, genau wie am Geraet),
  - Eingangsobjekte senden, auf Wunsch zyklisch (wie ein Sensor),
  - KNX-Bus und 12 V schalten, Funktionstaste druecken,
  - Ausgaenge S1...S4, Luefter-, Raum- und Taktzustand anzeigen,
  - Telegramme, Konsole und Invariantenwaechter mitschreiben.
"""

import argparse
import json
import os
import re
import threading
import time
import webbrowser
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse

from environment import PROFILES, QUANTITIES, SEASONS, STEP_MS, Environment
from occupant import BEHAVIOURS, Occupant
from kwlcore import BUILD, HERE, Core, ParamImage, effective_values, load_meta, visible_kos

WEB = os.path.join(HERE, 'web')
PROJECTS = os.path.join(HERE, 'projects')
TICK_S = 0.1          # Takt der Echtzeit
MAX_EVENTS = 6000
MAX_DAC = 400000


def safe_name(name):
    return re.sub(r'[^\w äöüÄÖÜß.+()-]', '_', name).strip() or 'Projekt'


class Simulator:
    def __init__(self, project):
        self.meta = load_meta()
        self.meta_json = json.dumps(self.meta, ensure_ascii=False).encode('utf-8')
        self.lock = threading.RLock()
        self.speed = 1.0
        self.events = []        # (seq, ev)
        self.seq = 0
        self.dac = []           # [t, v1, v2, v3, v4]
        self.kos = {}           # nr -> {val, t, dir, n, dpt}
        self.kos_version = 0
        self.cyclic = {}        # nr -> {dpt, value, period, next}
        self.state = None
        self.pending = ParamImage(self.meta)
        self.active_values = {}
        self.project = ''
        self.core = None
        self.env = Environment()
        self.occ = Occupant()
        self._button_ok = False
        self._env_cache = (None, None)
        self._start_core()
        self.load_project(project)

    # ------------------------------------------------------------ Kern
    def _on_event(self, ev):
        kind = ev.get('ev')
        if kind == 'dac':
            self.dac.append([ev['t']] + ev['v'])
            if len(self.dac) > MAX_DAC:
                del self.dac[: MAX_DAC // 4]
            return
        if kind == 'state':
            self.state = ev
            return
        if kind in ('ko', 'rx'):
            k = self.kos.setdefault(ev['nr'], {'n': 0})
            k.update(val=ev['val'], t=ev['t'], dir='tx' if kind == 'ko' else 'rx', dpt=ev['dpt'])
            k['n'] += 1
            self.kos_version += 1
        self.seq += 1
        self.events.append((self.seq, ev))
        if len(self.events) > MAX_EVENTS:
            del self.events[: MAX_EVENTS // 4]

    def _start_core(self):
        if self.core:
            try:
                self.core.close()
            except Exception:
                pass
        self.core = Core(on_event=self._on_event)
        self.dac.clear()
        self.kos.clear()
        self.kos_version += 1
        self.cyclic.clear()

    def cmd(self, line):
        with self.lock:
            return self.core.cmd(line)

    # ------------------------------------------------------------ Umgebung
    def _env_targets(self):
        """Aktive Raeume und ihre sichtbaren Sensorobjekte (ETS-Logik)."""
        sig = json.dumps(self.active_values, sort_keys=True)
        if self._env_cache[0] == sig:
            return self._env_cache[1]
        vals = effective_values(self.meta, self.active_values)
        vis = visible_kos(self.meta, vals)
        by_name = {k['name']: k['nr'] for k in self.meta['kos']}
        rooms, kos = [], {}
        for n in range(1, 9):
            if int(float(vals.get(f'ROOM_r{n}Active', 0))) != 1:
                continue
            rooms.append(n)
            m = {}
            for q in ('TempIn', 'HumIn', 'CO2', 'TempSet', 'TempOut', 'HumOut',
                      'Mode', 'Night', 'StageSet', 'Lock', 'ExhaustReq', 'Summer'):
                nr = by_name.get(f'ROOM_{n}{q}')
                if nr in vis:
                    m[q] = (nr, vis[nr])
            for name in (f'ROOM_{n}VOC', f'ROOM_{n}VOCByte'):
                nr = by_name.get(name)
                if nr in vis:
                    m['VOC'] = (nr, vis[nr])
                    break
            kos[n] = m
        # Funktionstaste kurz = Stossluefen? Nur dann drueckt der Bewohner sie.
        self._button_ok = int(float(vals.get('FAN_ButtonShort', 0))) == 1
        self._env_cache = (sig, (rooms, kos))
        return rooms, kos

    def _env_flows(self):
        """Volumenstrom je Raum aus den Objekten, die die Firmware sendet."""
        flows = {}
        for f in (self.state or {}).get('sim', {}).get('fans', []):
            nr = 340 + (f['no'] - 1) * 24 + 11            # FAN_nFlow
            v = self.kos.get(nr, {}).get('val', 0) or 0
            q, hrv = flows.get(f['room'], (0.0, False))
            flows[f['room']] = (q + abs(float(v)), hrv or bool(f.get('hrv')))
        return flows

    def _env_step(self, now):
        rooms, kos = self._env_targets()
        self.env.step(now, rooms, self._env_flows())
        if not (self.state or {}).get('mcu'):
            return                       # ohne Bus kein Empfang
        for nr, dpt, value in self.env.telegrams(now, kos):
            self.core.cmd(f'ko {nr} {dpt} {value}')
        # Bewohner: bedient ueber dieselben Objekte wie ein Taster
        for a in self.occ.step(now, self.env, kos, self._button_ok):
            if a[0] == 'btn':
                self.core.cmd(f'btn {a[1]}')
            else:
                self.core.cmd(f'ko {a[0]} {a[1]} {a[2]}')
            self._on_event({'ev': 'occupant', 't': now, 'room': a[-2], 'text': a[-1]})

    def run(self, ms):
        """Zeit fortschreiben; zyklische Telegramme und die Umgebung zur
        rechten Zeit einstreuen."""
        with self.lock:
            now = self.state['t'] if self.state else 0
            end = now + int(ms)
            env_on = self.env.config['enabled']
            if env_on and self.env.next < now:
                self.env.next = now
            while now < end:
                due = [c for c in self.cyclic.values() if c['next'] <= now]
                for c in due:
                    self.core.cmd(f"ko {c['nr']} {c['dpt']} {c['value']}")
                    c['next'] = now + c['period']
                if env_on and self.env.next <= now:
                    self._env_step(now)
                    self.env.next = now + STEP_MS
                nxt = min([c['next'] for c in self.cyclic.values()] + [end] +
                          ([self.env.next] if env_on else []))
                step = max(1, min(nxt, end) - now)
                self.core.cmd(f'run {step}')
                now += step
            self.core.cmd('state')

    def tick_loop(self):
        while True:
            t0 = time.time()
            if self.speed > 0:
                try:
                    self.run(TICK_S * 1000 * self.speed)
                except Exception as e:  # Kern abgestuerzt: melden, neu starten
                    self._on_event({'ev': 'error', 't': 0, 'text': f'Kern: {e} - Neustart des Simulators'})
                    with self.lock:
                        self._start_core()
                        self.load_project(self.project)
            time.sleep(max(0.0, TICK_S - (time.time() - t0)))

    # ------------------------------------------------------------ Projekte
    def project_path(self, name):
        return os.path.join(PROJECTS, safe_name(name) + '.json')

    def list_projects(self):
        if not os.path.isdir(PROJECTS):
            return []
        return sorted(f[:-5] for f in os.listdir(PROJECTS) if f.endswith('.json'))

    def load_project(self, name):
        """Projekt laden: Parameter setzen und das Geraet frisch starten -
        als waere es mit dieser Parametrierung programmiert und eingeschaltet."""
        data = {'values': {}}
        if name and os.path.exists(self.project_path(name)):
            with open(self.project_path(name), encoding='utf-8') as f:
                data = json.load(f)
        with self.lock:
            self._start_core()
            self.project = name or ''
            self.env.load(data.get('env'))
            self.occ.load(data.get('occupant'))
            self.pending.load_values(data.get('values', {}))
            self.active_values = dict(self.pending.values)
            for ch, g in enumerate(data.get('gain', [1, 1, 1, 1]), 1):
                self.core.cmd(f'gain {ch} {g}')
            self.core.cmd('load ' + self.pending.hex())
            self.core.cmd('v12 1')
            self.core.cmd('bus 1')
            self.core.cmd('run 500')
            self.core.cmd('state')

    def save_project(self, name):
        os.makedirs(PROJECTS, exist_ok=True)
        with open(self.project_path(name), 'w', encoding='utf-8') as f:
            json.dump({'name': name, 'values': self.pending.values, 'env': self.env.config,
                       'occupant': self.occ.config},
                      f, ensure_ascii=False, indent=1)
        self.project = name

    # ------------------------------------------------------------ Aktionen
    def action(self, a):
        op = a.get('action')
        if op == 'speed':
            self.speed = float(a['value'])
        elif op == 'step':
            self.run(int(a['ms']))
        elif op == 'power':
            self.cmd(f"{'bus' if a['which'] == 'bus' else 'v12'} {1 if a['on'] else 0}")
            self.cmd('state')
        elif op == 'ko':
            nr, dpt, value = int(a['nr']), a['dpt'], str(a['value']).strip()
            self.cmd(f'ko {nr} {dpt} {value}')
            period = float(a.get('cyclic') or 0)
            with self.lock:
                if period > 0:
                    now = self.state['t'] if self.state else 0
                    self.cyclic[nr] = dict(nr=nr, dpt=dpt, value=value, period=int(period * 1000),
                                           next=now + int(period * 1000))
                else:
                    self.cyclic.pop(nr, None)
            self.cmd('state')
        elif op == 'cyclic_off':
            with self.lock:
                self.cyclic.pop(int(a['nr']), None)
        elif op == 'btn':
            self.cmd(f"btn {a['kind']}")
            self.cmd('state')
        elif op == 'console':
            self.cmd(('diag ' if a.get('diag') else 'cmd ') + a['text'])
        elif op == 'param':
            with self.lock:
                self.pending.set(a['name'], a['value'])
        elif op == 'params_revert':
            with self.lock:
                self.pending.load_values(self.active_values)
        elif op == 'params_defaults':
            with self.lock:
                self.pending.reset()
        elif op == 'program':
            with self.lock:
                self.core.cmd('pimg ' + self.pending.hex())
                self.core.cmd(f"ets {int(a.get('ms', 3000))}")
                self.active_values = dict(self.pending.values)
                self.core.cmd('state')
        elif op == 'restart':
            self.cmd('restart')
            self.cmd('state')
        elif op == 'unload':
            self.cmd('unload')
            self.cmd('state')
        elif op == 'project_load':
            self.load_project(a['name'])
        elif op == 'project_save':
            self.save_project(a['name'])
        elif op == 'reset':
            self.load_project(self.project)
        elif op == 'gain':
            self.cmd(f"gain {int(a['ch'])} {float(a['value'])}")
        elif op == 'env':
            # Ein Sensor, den die Umgebung nicht mehr fuehrt, sendet nicht mehr;
            # der zuletzt gesendete Wert bleibt im Geraet stehen.
            with self.lock:
                self.env.configure(dict(a.get('patch', {})))
        elif op == 'occupant':
            with self.lock:
                self.occ.configure(dict(a.get('patch', {})))
        elif op == 'clear':
            with self.lock:
                self.events.clear()
        else:
            raise ValueError(f'unbekannte Aktion {op}')
        return {'ok': True}

    def env_poll(self, hist_since):
        e = self.env
        rooms, kos = self._env_targets()
        for n in rooms:
            e.room_config(n)
        hist_since = max(0, min(hist_since, len(e.history)))
        t = self.state['t'] if self.state else 0
        return dict(config=e.config, seasons=list(SEASONS), profiles=list(PROFILES),
                    quantities=list(QUANTITIES), rooms=rooms,
                    targets={n: {q: v[0] for q, v in m.items()} for n, m in kos.items()},
                    out=e.out, status=e.status, tod=e.tod_hours(t), day=e.day(t),
                    hist=e.history[hist_since:], histIdx=len(e.history),
                    occupant=dict(config=self.occ.config, behaviours=BEHAVIOURS,
                                  log=self.occ.log[-40:], plan=self.occ.plan))

    def poll(self, since, dac_since, kos_version, hist_since=0):
        with self.lock:
            ev = [e for s, e in self.events if s > since] if since >= 0 else [e for _, e in self.events[-500:]]
            ev = ev[-1500:]
            dac_since = max(0, min(dac_since, len(self.dac)))
            out = dict(seq=self.seq, events=ev, state=self.state, speed=self.speed,
                       dac=self.dac[dac_since:], dacIdx=len(self.dac),
                       project=self.project, projects=self.list_projects(),
                       pending=self.pending.values, active=self.active_values,
                       dirty=self.pending.values != self.active_values,
                       cyclic=[dict(nr=c['nr'], dpt=c['dpt'], value=c['value'],
                                    period=c['period'] / 1000) for c in self.cyclic.values()],
                       kosVersion=self.kos_version)
            if kos_version != self.kos_version:
                out['kos'] = self.kos
            out['env'] = self.env_poll(hist_since)
            return out


SIM = None


class Server(ThreadingHTTPServer):
    # Unter Windows erlaubt SO_REUSEADDR zwei Server am selben Port - der
    # zweite Start soll aber mit "Port belegt" scheitern, nicht still mitlaufen.
    allow_reuse_address = False
    daemon_threads = True


class Handler(BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, body, ctype='application/json; charset=utf-8'):
        if isinstance(body, (dict, list)):
            body = json.dumps(body, ensure_ascii=False).encode('utf-8')
        elif isinstance(body, str):
            body = body.encode('utf-8')
        self.send_response(code)
        self.send_header('Content-Type', ctype)
        self.send_header('Content-Length', str(len(body)))
        self.send_header('Cache-Control', 'no-store')
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urlparse(self.path)
        if url.path == '/api/meta':
            return self._send(200, SIM.meta_json)
        if url.path == '/api/poll':
            q = parse_qs(url.query)
            return self._send(200, SIM.poll(int(q.get('since', ['-1'])[0]),
                                            int(q.get('dac', ['0'])[0]),
                                            int(q.get('kv', ['-1'])[0]),
                                            int(q.get('eh', ['0'])[0])))
        name = 'index.html' if url.path in ('/', '') else url.path.lstrip('/')
        path = os.path.normpath(os.path.join(WEB, name))
        if not path.startswith(WEB) or not os.path.isfile(path):
            return self._send(404, {'error': 'nicht gefunden'})
        ctype = {'.html': 'text/html; charset=utf-8', '.js': 'text/javascript; charset=utf-8',
                 '.css': 'text/css; charset=utf-8', '.svg': 'image/svg+xml'}.get(
            os.path.splitext(path)[1], 'application/octet-stream')
        with open(path, 'rb') as f:
            self._send(200, f.read(), ctype)

    def do_POST(self):
        if urlparse(self.path).path != '/api/action':
            return self._send(404, {'error': 'nicht gefunden'})
        try:
            data = json.loads(self.rfile.read(int(self.headers.get('Content-Length', 0))) or b'{}')
            self._send(200, SIM.action(data))
        except Exception as e:
            self._send(400, {'ok': False, 'error': str(e)})


def main():
    global SIM
    ap = argparse.ArgumentParser(description='KWL-Simulator (Lüftungsmodul KNXFANDRV)')
    ap.add_argument('--port', type=int, default=8765)
    ap.add_argument('--no-browser', action='store_true')
    ap.add_argument('--project', default='Aufbau B (Testplan)')
    args = ap.parse_args()

    if not os.path.exists(os.path.join(BUILD, 'kwlsim.exe')):
        raise SystemExit('build/kwlsim.exe fehlt - zuerst: powershell -File sim/build.ps1')
    try:
        server = Server(('127.0.0.1', args.port), Handler)
    except OSError:
        raise SystemExit(f'Port {args.port} ist belegt - läuft der Simulator schon? '
                         f'Sonst mit --port einen anderen wählen.')
    SIM = Simulator(args.project)
    threading.Thread(target=SIM.tick_loop, daemon=True).start()
    url = f'http://127.0.0.1:{args.port}/'
    print(f'KWL-Simulator läuft: {url}   (Beenden mit Strg+C)')
    if not args.no_browser:
        webbrowser.open(url)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        SIM.core.close()


if __name__ == '__main__':
    main()
