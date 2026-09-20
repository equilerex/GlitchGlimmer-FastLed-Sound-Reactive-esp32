import { createApp } from 'vue';
import { state } from './state.js';
import * as recording from './app.js';
import * as live from './live.js';
import { PROFILES, profileById, countForLength } from './viz/profiles.js';
import { SURFACES } from './viz/surface.js';
import { SHAPES } from './viz/path.js';
import { BenchStrip } from './viz/BenchStrip.js';
import { loadHw, resetHw, bindView } from './viz/hwStore.js';

const app = createApp({
  data() {
    return {
      s: state,
      profiles: PROFILES,
      surfaces: SURFACES,
      shapes: Object.keys(SHAPES),
      boundView: null,
      bench: null,
      benchCounts: null,
      hwLoaded: false
    };
  },
  computed: {
    profileNote() {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      return strip ? profileById(strip.profile).note : '';
    },
    density() {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      return strip && strip.pitchMm > 0 ? 1000 / strip.pitchMm : 60;
    },
    zoomPercent() {
      const zoom = Number.isFinite(this.s.hw.zoom) ? this.s.hw.zoom : 1;
      return Math.round((zoom - 1) * 100);
    }
  },
  methods: {
    async selectMode(next) {
      if (next === this.s.mode) return;

      if (this.s.mode === 'recording') recording.stop();
      if (this.s.mode === 'live') live.stop();

      this.s.mode = next;

      if (next === 'recording') await recording.start();
      else await live.start();

      this.bindRendererWhenReady();
    },
    
    // Recording actions
    togglePlay() {
      recording.togglePlay();
    },
    scrubTo(event) {
      recording.scrubTo(parseInt(event.target.value));
    },
    playScenario(index) {
      recording.playScenario(index);
    },

    // Live actions
    useMic() {
      live.setSource('mic');
    },
    useDemo() {
      live.setSource('demo');
    },
    updateTuning(index, value) {
      live.updateTuning(index, parseFloat(value));
    },

    toggleHwDrawer() {
      this.s.hwDrawerOpen = !this.s.hwDrawerOpen;
      // #app is the mount element, so a :class binding on it never compiles.
      document.getElementById('app').classList.toggle('hw-open', this.s.hwDrawerOpen);
      // Docking changes the stage width without a window resize; the canvases
      // only re-measure on that event.
      this.$nextTick(() => window.dispatchEvent(new Event('resize')));
    },
    resetHwSettings() {
      if (this.boundView) {
        const fresh = resetHw(this.boundView.counts);
        Object.assign(this.s.hw, fresh);
        this.syncHw();
      }
    },
    // Hardware rail actions
    syncHw() {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (strip) {
        const capacity = live.softwareStripCapacity(this.s.hw.activeStrip) || Infinity;
        const count = countForLength(strip.lengthM, strip.pitchMm, capacity || Infinity);
        live.setSoftwareStripLength(this.s.hw.activeStrip, count);
        if (this.bench && this.boundView) this.bench.setCounts(this.boundView.counts);
      }
      if (this.syncView) this.syncView();
    },
    selectProfile(id) {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      strip.profile = id;
      const profile = profileById(id);
      if (!strip.pitchManual) strip.pitchMm = profile.pitch;
      strip.pixelSize = profile.visual.pixelSize;
      strip.glowSize = profile.visual.glowSize;
      strip.intensity = profile.visual.intensity;
      this.syncHw();
    },
    setDensity(event) {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      const density = Math.max(3, Number(event.target.value) || 60);
      strip.pitchMm = 1000 / density;
      strip.pitchManual = true;
      this.syncHw();
    },
    editPitch() {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      strip.pitchManual = true;
      this.syncHw();
    },
    setZoomPercent(event) {
      const percent = Math.max(-90, Math.min(900, Number(event.target.value) || 0));
      this.s.hw.zoom = 1 + percent / 100;
      this.syncHw();
    },
    applyShape(name) {
      const strip = this.s.hw.strips[this.s.hw.activeStrip];
      if (!strip) return;
      strip.shape = name;
      strip.pts = SHAPES[name].map((p) => p.slice());
      this.syncHw();
    },
    // Drawn from the profile's own casing rather than a per-type image, for the
    // same reason the renderer has no per-type branch: a new strip type should
    // be a row in the table and nothing else.
    swatchFor(p) {
      if (p.casing === 'cob') return 'linear-gradient(90deg,#f6d9a8,#ffe9c4)';
      if (p.casing === 'sleeve') return 'linear-gradient(90deg,#3b4049,#5a6170)';
      if (p.casing === 'pip') return 'repeating-linear-gradient(90deg,#2a2f38 0 6px,#8ad6ff 6px 7px)';
      if (p.casing === 'bulb') return 'repeating-linear-gradient(90deg,#20252c 0 8px,#ffd9a8 8px 11px)';
      return 'repeating-linear-gradient(90deg,#1b1f26 0 4px,#d9e4ff 4px 6px)';
    },

    // app.js and live.js each build their own StripView on the same canvas, so
    // switching mode replaces the instance underneath us. Binding therefore has
    // to be repeatable rather than a one-time hookup: without this the bench,
    // the inspector and the fit readout stop updating after the first switch.
    bindRenderer() {
      const view = document.getElementById('view').__view;
      if (!view || view === this.boundView) return !!view;

      if (!this.hwLoaded) {
        Object.assign(this.s.hw, loadHw(view.counts));
        this.hwLoaded = true;
      }
      if (!this.bench) {
        this.bench = new BenchStrip(document.getElementById('bench'), view.counts);
        this.benchCounts = view.counts.slice();
        // resize() reallocates the canvas backing store, which clears it, and
        // nothing repaints it until the next frame — with a paused recording
        // there is no next frame, so the bench would stay blank until playback
        // resumes. Repaint it from the bound view's last bytes immediately.
        window.addEventListener('resize', () => {
          this.bench.resize();
          if (this.boundView && this.boundView.lastBytes) {
            this.bench.paint(this.boundView.lastBytes, this.s.hw);
          }
        });
      } else if (view.counts.length !== this.benchCounts.length ||
                 view.counts.some((c, i) => c !== this.benchCounts[i])) {
        // The live engine's counts come from Config.h, the recording manifest's
        // from web/data/manifest.json — two different sources that only agree by
        // convention. If they ever diverge, rebuild rather than let the bench go
        // on rendering the previous mode's pixel counts.
        this.bench = new BenchStrip(document.getElementById('bench'), view.counts);
        this.benchCounts = view.counts.slice();
      }

      // The old view keeps its six pointer listeners on this canvas and its
      // own stale activeStrip/width/height forever unless told otherwise —
      // detach it before the new view takes over, and null its callbacks so
      // nothing it still holds a reference to can fire into state it no
      // longer owns.
      if (this.boundView && this.boundView !== view) {
        this.boundView.detach();
        this.boundView.onFrame = null;
        this.boundView.onHover = null;
        this.boundView.onGeometryChange = null;
        this.boundView.onPathCommit = null;
      }

      this.boundView = view;
      this.syncView = bindView(view, this.bench, this.s.hw, this.s);
      const commitPath = view.onPathCommit;
      view.onPathCommit = () => {
        if (commitPath) commitPath();
        this.syncHw();
      };
      this.syncView();

      // Plain DOM writes, not Vue bindings: these update every frame, and a
      // reactive write per frame costs more than the readout is worth.
      //
      // bindView above reassigns view.onFrame on every call, so this wrap has to
      // be applied here — after bindView, inside the branch that only runs once
      // per newly bound view (the `view === this.boundView` guard above already
      // returned early otherwise). Doing it anywhere else either loses the wrap
      // on the next mode switch, since bindView would overwrite it again, or
      // stacks a new wrapper on top of the previous one every time bindRenderer
      // is polled by bindRendererWhenReady.
      const cells = {
        count: document.getElementById('t-count'),
        pitch: document.getElementById('t-pitch'),
        draw: document.getElementById('t-draw'),
        frame: document.getElementById('t-frame'),
        power: document.getElementById('t-power'),
      };
      let lastFrameAt = 0;
      let frameMs = 16;
      const paintBench = view.onFrame;

      view.onFrame = (bytes) => {
        paintBench(bytes);

        const now = performance.now();
        if (lastFrameAt) frameMs += ((now - lastFrameAt) - frameMs) * 0.08;
        lastFrameAt = now;

        const strip = this.s.hw.strips[this.s.hw.activeStrip];
        const profile = profileById(strip ? strip.profile : '');

        // 60 mA per pixel at full white is the number every WS2812 supply is
        // sized against, so the readout is in the unit the decision gets made
        // in rather than in normalised brightness.
        let duty = 0;
        for (let i = 0; i + 2 < bytes.length; i += 3) {
          duty += (bytes[i] + bytes[i + 1] + bytes[i + 2]) / 765;
        }
        const amps = (duty * 60) / 1000;

        cells.count.textContent = view.counts.join(' + ');
        cells.pitch.textContent = profile.pitch.toFixed(1) + ' mm';
        cells.draw.textContent = this.s.hw.drawMs.toFixed(1) + ' ms';
        cells.frame.textContent = frameMs.toFixed(1) + ' ms';
        cells.power.textContent =
          'draw ≈ ' + amps.toFixed(2) + ' A @ 5 V · ' + (amps * 5).toFixed(1) + ' W';
      };

      return true;
    },

    // The new instance appears somewhere inside an async start() that gives no
    // signal, so the arrival has to be polled for. Bounded, because a mode that
    // never starts should not leave a timer running for the session.
    bindRendererWhenReady(attempts = 40) {
      if (this.bindRenderer() || attempts <= 0) return;
      setTimeout(() => this.bindRendererWhenReady(attempts - 1), 100);
    }
  },
  mounted() {
    document.getElementById('app').classList.toggle('hw-open', this.s.hwDrawerOpen);
    live.init();
    const params = new URLSearchParams(window.location.search);
    const initialMode = params.get('mode') === 'recording' ? 'recording' : 'live';
    this.s.mode = null; // force change
    this.selectMode(initialMode);
  }
});

app.mount('#app');
