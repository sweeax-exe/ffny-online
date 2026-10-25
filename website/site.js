/* defjam.online: the download button, the screenshot lightbox, the gameplay clip and the embers.
   Everything here is optional: without JS the button still points at the latest GitHub release. */
(function () {
  'use strict';

  var REPO = 'sweeax-exe/ffny-online';
  var API = 'https://api.github.com/repos/' + REPO + '/releases/latest';
  var CACHE_KEY = 'ffny-latest-release-v1';
  var CACHE_MS = 15 * 60 * 1000; // the API allows 60 requests an hour per address

  var PATTERNS = {
    windows: /^DefJamSetup-.+-windows-x64\.exe$/i,
    macos: /^DefJamSetup-.+-macos-arm64\.dmg$/i,
    linux: /^DefJamSetup-.+-linux-x64\.tar\.gz$/i
  };

  function megabytes(bytes) {
    var mb = bytes / (1024 * 1024);
    return (mb >= 10 ? Math.round(mb) : mb.toFixed(1)) + ' MB';
  }

  function shortDate(iso) {
    try {
      return new Date(iso).toLocaleDateString('en-GB', { day: 'numeric', month: 'short', year: 'numeric' });
    } catch (e) {
      return '';
    }
  }

  // Keep only what the page needs; the full API answer carries the release notes and more.
  function summarise(release) {
    var out = { tag: release.tag_name || '', url: release.html_url || '', date: release.published_at || '', assets: {} };
    var assets = release.assets || [];
    var byName = {};
    assets.forEach(function (a) { byName[a.name] = a; });
    Object.keys(PATTERNS).forEach(function (os) {
      for (var i = 0; i < assets.length; i++) {
        var a = assets[i];
        if (PATTERNS[os].test(a.name)) {
          var sum = byName[a.name + '.sha256'];
          out.assets[os] = {
            name: a.name,
            url: a.browser_download_url,
            size: a.size,
            sha256: sum ? sum.browser_download_url : ''
          };
          break;
        }
      }
    });
    return out;
  }

  function readCache() {
    try {
      var raw = window.sessionStorage.getItem(CACHE_KEY);
      if (!raw) return null;
      var item = JSON.parse(raw);
      if (!item || Date.now() - item.at > CACHE_MS) return null;
      return item.data;
    } catch (e) {
      return null;
    }
  }

  function writeCache(data) {
    try {
      window.sessionStorage.setItem(CACHE_KEY, JSON.stringify({ at: Date.now(), data: data }));
    } catch (e) { /* private mode or storage off: fine, we just ask again next time */ }
  }

  function link(text, href, extra) {
    var li = document.createElement('li');
    var a = document.createElement('a');
    a.href = href;
    a.rel = 'noopener';
    a.textContent = text;
    li.appendChild(a);
    if (extra) {
      var span = document.createElement('span');
      span.className = 'dl-size';
      span.textContent = ' ' + extra;
      li.appendChild(span);
    }
    return li;
  }

  function showRelease(info) {
    var win = info && info.assets && info.assets.windows;
    var more = document.getElementById('dl-more');
    if (!win || !more) return;
    var button = document.getElementById('dl-main');
    var meta = document.getElementById('dl-meta');
    var version = info.tag.replace(/^v/, '');
    var build = /remaster\.(\d+)$/.exec(version);
    button.href = win.url;
    meta.textContent = (build ? 'Build ' + build[1] + ' · ' : '') + 'v' + version + ' · ' +
      megabytes(win.size) + (info.date ? ' · ' + shortDate(info.date) : '');
    button.setAttribute('aria-label', 'Download for Windows, ' + win.name + ', ' + megabytes(win.size));

    more.textContent = '';
    if (info.assets.macos) more.appendChild(link('macOS (Apple Silicon)', info.assets.macos.url, megabytes(info.assets.macos.size)));
    if (info.assets.linux) more.appendChild(link('Linux / Steam Deck', info.assets.linux.url, megabytes(info.assets.linux.size)));
    if (win.sha256) more.appendChild(link('SHA-256', win.sha256));
    more.appendChild(link('Release notes', info.url || 'https://github.com/' + REPO + '/releases/latest'));
    more.hidden = false;
  }

  function loadRelease() {
    var cached = readCache();
    if (cached) { showRelease(cached); return; }
    if (!window.fetch) return;
    fetch(API, { headers: { Accept: 'application/vnd.github+json' }, credentials: 'omit', referrerPolicy: 'no-referrer' })
      .then(function (r) { if (!r.ok) throw new Error('GitHub API ' + r.status); return r.json(); })
      .then(function (release) {
        var info = summarise(release);
        if (info.assets.windows) writeCache(info);
        showRelease(info);
      })
      .catch(function () { /* keep the fallback link to the latest release */ });
  }

  // ---- screenshots open in a lightbox ----
  function setupLightbox() {
    var box = document.getElementById('lightbox');
    var img = document.getElementById('lightbox-img');
    if (!box || typeof box.showModal !== 'function') return; // the links still open the picture
    document.addEventListener('click', function (e) {
      var a = e.target.closest && e.target.closest('a[data-full]');
      if (!a || e.ctrlKey || e.metaKey || e.shiftKey || e.button !== 0) return;
      e.preventDefault();
      var thumb = a.querySelector('img');
      img.src = a.getAttribute('data-full');
      img.alt = thumb ? thumb.alt : '';
      box.showModal();
    });
    box.addEventListener('click', function (e) { if (e.target === box) box.close(); });
  }

  // ---- the gameplay clip loads only on request (it is 6.6 MB) ----
  function setupClip() {
    var play = document.getElementById('clip-play');
    var img = document.getElementById('clip-img');
    if (!play || !img) return;
    play.addEventListener('click', function (e) {
      e.preventDefault();
      img.src = play.getAttribute('data-gif');
      img.alt = 'A short, silent fight clip from the PC port';
      play.remove();
    });
  }

  // ---- embers rising through the hero; off for reduced motion, paused off screen ----
  function setupEmbers() {
    var canvas = document.querySelector('.embers');
    if (!canvas || !canvas.getContext) return;
    if (window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    var ctx = canvas.getContext('2d');
    var dpr = Math.min(window.devicePixelRatio || 1, 2);
    var w = 0, h = 0, embers = [], running = false, visible = true, last = 0;

    function spawn(anywhere) {
      return {
        x: Math.random() * w,
        y: anywhere ? Math.random() * h : h + 10 * dpr,
        r: (0.6 + Math.random() * 1.8) * dpr,
        vy: (12 + Math.random() * 30) * dpr,
        vx: (Math.random() - 0.5) * 10 * dpr,
        sway: Math.random() * Math.PI * 2,
        life: 0.4 + Math.random() * 0.6,
        hue: 12 + Math.random() * 26
      };
    }

    function resize() {
      var rect = canvas.getBoundingClientRect();
      w = canvas.width = Math.max(1, Math.round(rect.width * dpr));
      h = canvas.height = Math.max(1, Math.round(rect.height * dpr));
      var count = Math.round(Math.min(70, Math.max(24, rect.width / 22)));
      embers = [];
      for (var i = 0; i < count; i++) embers.push(spawn(true));
    }

    function frame(t) {
      if (!running) return;
      var dt = Math.min(0.05, (t - (last || t)) / 1000);
      last = t;
      ctx.clearRect(0, 0, w, h);
      ctx.globalCompositeOperation = 'lighter';
      for (var i = 0; i < embers.length; i++) {
        var p = embers[i];
        p.sway += dt * 1.3;
        p.y -= p.vy * dt;
        p.x += (p.vx + Math.sin(p.sway) * 8 * dpr) * dt;
        var fade = Math.max(0, Math.min(1, p.y / h)) * p.life;
        if (p.y < -10 || fade <= 0.01) { embers[i] = spawn(false); continue; }
        var g = ctx.createRadialGradient(p.x, p.y, 0, p.x, p.y, p.r * 4);
        g.addColorStop(0, 'hsla(' + p.hue + ', 100%, 65%, ' + fade + ')');
        g.addColorStop(0.35, 'hsla(' + p.hue + ', 100%, 50%, ' + fade * 0.35 + ')');
        g.addColorStop(1, 'hsla(' + p.hue + ', 100%, 45%, 0)');
        ctx.fillStyle = g;
        ctx.beginPath();
        ctx.arc(p.x, p.y, p.r * 4, 0, Math.PI * 2);
        ctx.fill();
      }
      requestAnimationFrame(frame);
    }

    function update() {
      var should = visible && !document.hidden;
      if (should && !running) { running = true; last = 0; requestAnimationFrame(frame); }
      else if (!should) running = false;
    }

    resize();
    var resizeTimer;
    window.addEventListener('resize', function () {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(resize, 150);
    });
    document.addEventListener('visibilitychange', update);
    if ('IntersectionObserver' in window) {
      new IntersectionObserver(function (entries) {
        visible = entries[0].isIntersecting;
        update();
      }).observe(canvas);
    }
    update();
  }

  function start() {
    loadRelease();
    setupLightbox();
    setupClip();
    setupEmbers();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
