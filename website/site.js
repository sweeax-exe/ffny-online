/* defjam.online: the download button, the screenshot lightbox, the gameplay clip, the embers and the
   ranked ladder (ranked.html), the Turf War map (turf.html) and the Studio gallery (studio.html).
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

  // ---- tournaments, from the account service (server/docs/tournaments.md) ----
  var TOURNAMENTS = 'https://play.defjam.online';

  // A local preview may point at a local account service: http://localhost:8000/?api=http://localhost:8095
  function tournamentsBase() {
    var host = window.location.hostname;
    var m = /[?&]api=(http:\/\/(?:localhost|127\.0\.0\.1):\d+)/.exec(window.location.search);
    return (m && (host === 'localhost' || host === '127.0.0.1')) ? m[1] : TOURNAMENTS;
  }

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }

  function localWhen(iso) {
    try {
      return new Date(iso).toLocaleString(undefined, { weekday: 'short', day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit', timeZoneName: 'short' });
    } catch (e) {
      return iso;
    }
  }

  function tournamentCard(t, base) {
    var a = el('a', 'glass tourney');
    var id = Number(t.id);
    a.href = base + '/tournaments/' + (id > 0 ? id : '');
    var top = el('div', 'tourney-top');
    var status = el('span', 'status s-' + String(t.status).replace(/[^a-z_]/g, ''), t.status_label);
    var tags = el('span', 'tourney-tags');
    tags.appendChild(status);
    if (t.startgg) tags.appendChild(el('span', 'tourney-sgg', 'start.gg'));
    top.appendChild(tags);
    top.appendChild(el('span', 'tourney-region', t.region_label));
    a.appendChild(top);
    a.appendChild(el('h3', null, t.name));
    var when = el('p', 'tourney-when', localWhen(t.start));
    a.appendChild(when);
    a.appendChild(el('p', 'tourney-meta', t.format_label + ' · Bo' + t.set_length));
    var slots = el('div', 'tourney-slots');
    if (t.startgg_signup) { // sign-up and bracket on start.gg: our count would read 0
      var cap = el('span');
      cap.appendChild(document.createTextNode('Up to '));
      cap.appendChild(el('b', null, String(t.max_players)));
      cap.appendChild(document.createTextNode(' players · sign-up on start.gg'));
      slots.appendChild(cap);
      a.appendChild(slots);
      a.appendChild(el('p', 'tourney-by', 'by ' + t.creator));
      return a;
    }
    var bar = el('progress');
    bar.max = Math.max(1, t.max_players);
    bar.value = Math.min(t.accepted, t.max_players);
    slots.appendChild(bar);
    var count = el('span');
    count.appendChild(el('b', null, String(t.accepted)));
    count.appendChild(document.createTextNode('/' + t.max_players + ' players'));
    slots.appendChild(count);
    a.appendChild(slots);
    a.appendChild(el('p', 'tourney-by', 'by ' + t.creator));
    return a;
  }

  function loadTournaments() {
    var section = document.getElementById('tournaments');
    var cards = document.getElementById('tournament-cards');
    if (!section || !cards || !window.fetch) return;
    var base = tournamentsBase();
    fetch(base + '/api/tournaments', { credentials: 'omit', referrerPolicy: 'no-referrer' })
      .then(function (r) { if (!r.ok) throw new Error('tournaments ' + r.status); return r.json(); })
      .then(function (data) {
        var list = (data && data.tournaments) || [];
        cards.textContent = '';
        list.slice(0, 6).forEach(function (t) { cards.appendChild(tournamentCard(t, base)); });
        document.getElementById('tournaments-empty').hidden = list.length > 0;
        document.getElementById('tournaments-all').href = base + '/tournaments';
        section.hidden = false;
        var nav = document.getElementById('nav-tournaments');
        if (nav) nav.hidden = false;
      })
      .catch(function () { /* tournaments off or the service away: the section stays hidden */ });
  }

  // ---- Turf War (turf.html and the start page teaser), from the account service ----
  // GET /api/turf and GET /api/turf/season answer 404 while Turf War is off.
  var BOROUGHS = [['bronx', 'The Bronx'], ['manhattan', 'Manhattan'], ['queens', 'Queens'],
                  ['brooklyn', 'Brooklyn'], ['staten_island', 'Staten Island'], ['', 'Elsewhere in the city']];

  function turfGet(base, path) {
    return fetch(base + path, { credentials: 'omit', referrerPolicy: 'no-referrer' }).then(function (r) {
      if (r.status === 404) { var e = new Error('off'); e.off = true; throw e; }
      if (!r.ok) throw new Error('turf ' + r.status);
      return r.json();
    });
  }

  function crewLabel(c) {
    var s = el('span', 'turf-crew');
    s.appendChild(el('b', null, '[' + (c.tag || '?') + ']'));
    s.appendChild(document.createTextNode(' ' + (c.name || '')));
    return s;
  }

  function realDate(iso) {
    var d = iso && new Date(iso);
    return d && !isNaN(d.getTime()) && d.getFullYear() > 2000 ? d : null;
  }

  function venueCard(v, now) {
    var card = el('div', 'glass turf-venue' + (v.boss ? ' is-boss' : '') + (v.owner ? ' is-held' : ' is-open'));
    var top = el('div', 'turf-venue-top');
    top.appendChild(el('h4', null, v.name || '?'));
    if (v.boss) top.appendChild(el('span', 'turf-badge boss', 'Boss'));
    card.appendChild(top);
    var holder = el('p', 'turf-holder');
    if (v.owner) holder.appendChild(crewLabel(v.owner));
    else holder.appendChild(el('span', 'turf-open', 'OPEN'));
    card.appendChild(holder);
    var tags = el('div', 'turf-tags');
    var prot = realDate(v.protected_until);
    if (v.owner && prot && prot > now) tags.appendChild(el('span', 'turf-badge prot', 'Protected until ' + localWhen(v.protected_until)));
    if (v.owner && v.defended > 0) tags.appendChild(el('span', 'turf-badge', 'Defended ' + v.defended + 'x'));
    var ch = v.challenge;
    if (ch && ch.challenger) {
      var c = el('span', 'turf-badge chal');
      c.appendChild(document.createTextNode('Challenged by [' + (ch.challenger.tag || '?') + '] ' + (ch.challenger.name || '')));
      if (realDate(ch.deadline)) c.appendChild(document.createTextNode(', answer by ' + localWhen(ch.deadline)));
      tags.appendChild(c);
    } else if (!v.owner && v.claim_battle) {
      tags.appendChild(el('span', 'turf-badge chal', 'Claim battle under way'));
    }
    if (tags.childNodes.length) card.appendChild(tags);
    return card;
  }

  function renderTurfMap(map) {
    var wrap = document.getElementById('turf-boroughs');
    wrap.textContent = '';
    var now = new Date();
    var venues = (map && map.venues) || [];
    BOROUGHS.forEach(function (b) {
      var list = venues.filter(function (v) { return (v.borough || '') === b[0]; });
      if (!list.length) return;
      var group = el('section', 'turf-borough');
      group.appendChild(el('h3', 'turf-h', b[1]));
      var grid = el('div', 'turf-grid');
      list.forEach(function (v) { grid.appendChild(venueCard(v, now)); });
      group.appendChild(grid);
      wrap.appendChild(group);
    });
    var line = document.getElementById('turf-season');
    line.textContent = '';
    line.appendChild(el('strong', null, 'Season ' + (map.season || '') + (map.month ? ' (' + map.month + ')' : '')));
    if (realDate(map.ends)) line.appendChild(document.createTextNode(' ends ' + seasonEnd(map.ends) + '.'));
    var r = map.rules;
    if (r && r.response_hours) {
      line.appendChild(document.createTextNode(' A holder has ' + r.response_hours + ' hours to answer a challenge.'));
    }
    line.hidden = false;
  }

  function renderTurfSeason(s) {
    var rows = document.getElementById('turf-standings-rows');
    rows.textContent = '';
    var st = (s && s.standings) || [];
    document.getElementById('turf-standings').hidden = st.length === 0;
    document.getElementById('turf-standings-empty').hidden = st.length > 0;
    st.forEach(function (e) {
      var tr = document.createElement('tr');
      tr.appendChild(el('td', 'c-place', String(e.place || '')));
      var name = el('td', 'c-name');
      name.appendChild(crewLabel(e));
      tr.appendChild(name);
      tr.appendChild(el('td', 'c-num', String(e.points || 0)));
      tr.appendChild(el('td', 'c-num', String(e.venues || 0)));
      tr.appendChild(el('td', 'c-num', e.rating != null ? String(e.rating) : ''));
      rows.appendChild(tr);
    });
    var hist = (s && s.history) || [];
    var hrows = document.getElementById('turf-history-rows');
    hrows.textContent = '';
    hist.forEach(function (h) {
      var top = h.top && h.top[0];
      if (!top) return;
      var tr = document.createElement('tr');
      tr.appendChild(el('td', null, 'Season ' + h.season + (h.month ? ' (' + h.month + ')' : '')));
      var name = el('td', 'c-name');
      name.appendChild(crewLabel(top));
      tr.appendChild(name);
      tr.appendChild(el('td', 'c-num', String(top.points || 0)));
      tr.appendChild(el('td', 'c-num', String(top.venues || 0)));
      hrows.appendChild(tr);
    });
    document.getElementById('turf-history-wrap').hidden = hrows.childNodes.length === 0;
  }

  function setupTurf() {
    var msg = document.getElementById('turf-msg');
    var body = document.getElementById('turf-body');
    if (!msg || !window.fetch) return;
    var base = tournamentsBase();
    var shown = false;
    function say(text, retry) {
      msg.textContent = '';
      var p = el('p', 'ladder-msg', text);
      if (retry) {
        var b = el('button', null, 'Try again');
        b.type = 'button';
        b.addEventListener('click', load);
        p.appendChild(b);
      }
      msg.appendChild(p);
      msg.hidden = false;
    }
    function load() {
      Promise.all([turfGet(base, '/api/turf'), turfGet(base, '/api/turf/season')]).then(function (res) {
        renderTurfMap(res[0]);
        renderTurfSeason(res[1]);
        msg.hidden = true;
        body.hidden = false;
        shown = true;
        document.getElementById('turf-updated').textContent = 'Updated ' + new Date().toLocaleTimeString() + '. The map refreshes every minute.';
      }).catch(function (e) {
        if (e && e.off) {
          body.hidden = true;
          document.getElementById('turf-season').hidden = true;
          say('Turf War starts soon. Crews will fight for the venues of New York here; the map goes live with the first season.');
        } else if (!shown) {
          say('The map could not be loaded.', true);
        } /* else keep showing the last map */
      });
    }
    load();
    setInterval(function () { if (!document.hidden) load(); }, 60000);
  }

  function loadTurfTeaser() {
    var section = document.getElementById('turf');
    if (!section || !window.fetch) return;
    turfGet(tournamentsBase(), '/api/turf').then(function (map) {
      var venues = (map && map.venues) || [];
      var held = venues.filter(function (v) { return v.owner; }).length;
      var stat = document.getElementById('turf-teaser-stat');
      if (stat && venues.length) stat.textContent = held + ' of ' + venues.length + ' venues are held right now.';
      section.hidden = false;
      var nav = document.getElementById('nav-turf');
      if (nav) nav.hidden = false;
    }).catch(function () { /* Turf War off or the service away: the section stays hidden */ });
  }

  // ---- the ranked ladder (ranked.html), from the account service ----
  function seasonEnd(iso) {
    try {
      return new Date(iso).toLocaleDateString(undefined, { day: 'numeric', month: 'long', year: 'numeric' });
    } catch (e) {
      return iso;
    }
  }

  function loadSeason(base) {
    var line = document.getElementById('season');
    if (!line) return;
    fetch(base + '/api/ranked/seasons', { credentials: 'omit', referrerPolicy: 'no-referrer' })
      .then(function (r) { if (!r.ok) throw new Error('seasons ' + r.status); return r.json(); })
      .then(function (data) {
        var now = data && data.now;
        if (!now || !now.name) return;
        line.textContent = '';
        line.appendChild(el('strong', null, now.name));
        var end = now.end && new Date(now.end);
        if (end && !isNaN(end.getTime()) && end.getFullYear() > 2000) {
          line.appendChild(document.createTextNode((end > new Date() ? ' ends ' : ' ended ') + seasonEnd(now.end) + '.'));
        }
        line.hidden = false;
      })
      .catch(function () { /* the season line stays hidden */ });
  }

  function formerNames(entry) {
    var f = entry.formerly;
    if (typeof f === 'string') f = f ? [f] : [];
    if (!Array.isArray(f)) return [];
    return f.filter(function (n) { return typeof n === 'string' && n; });
  }

  function ladderRow(entry, provisional) {
    var tr = document.createElement('tr');
    tr.appendChild(el('td', 'c-place', provisional ? '' : String(entry.place || '')));
    var name = el('td', 'c-name', entry.name || '?');
    var former = formerNames(entry);
    if (former.length) name.appendChild(el('small', 'formerly', 'formerly ' + former.join(', ')));
    tr.appendChild(name);
    tr.appendChild(el('td', 'c-rank', (entry.rank || '') + (provisional ? ' ?' : '')));
    tr.appendChild(el('td', 'c-num', entry.rating != null ? String(entry.rating) : ''));
    tr.appendChild(el('td', 'c-num', (entry.set_wins || 0) + '-' + (entry.set_losses || 0)));
    return tr;
  }

  function setupLadder() {
    var panel = document.getElementById('ladder-panel');
    if (!panel || !window.fetch) return;
    var base = tournamentsBase();
    var tabs = Array.prototype.slice.call(document.querySelectorAll('.ladder-tabs [role="tab"]'));
    var msg = document.getElementById('ladder-msg');
    var table = document.getElementById('ladder-table');
    var rows = document.getElementById('ladder-rows');
    var moreWrap = document.getElementById('ladder-more-wrap');
    var more = document.getElementById('ladder-more');
    var settling = document.getElementById('settling');
    var settlingRows = document.getElementById('settling-rows');
    var state = { mode: 'open', shown: 0, total: 0, settled: 0, request: 0 };

    function message(text, retry, append) {
      msg.textContent = text;
      if (retry) {
        var b = el('button', null, 'Try again');
        b.type = 'button';
        b.addEventListener('click', function () { load(append); });
        msg.appendChild(b);
      }
      msg.hidden = false;
    }

    function load(append) {
      var id = ++state.request;
      var offset = append ? state.shown : 0;
      if (!append) {
        rows.textContent = '';
        settlingRows.textContent = '';
        table.hidden = true;
        settling.hidden = true;
        moreWrap.hidden = true;
        state.shown = 0;
        state.settled = 0;
        message('Loading the ladder…');
      } else {
        more.disabled = true;
      }
      var url = base + '/api/ranked/leaderboard?mode=' + encodeURIComponent(state.mode) + '&provisional=1' +
        (offset ? '&offset=' + offset : '');
      fetch(url, { credentials: 'omit', referrerPolicy: 'no-referrer' })
        .then(function (r) { if (!r.ok) throw new Error('leaderboard ' + r.status); return r.json(); })
        .then(function (data) {
          if (id !== state.request) return;
          var entries = (data && data.entries) || [];
          entries.forEach(function (e) { rows.appendChild(ladderRow(e, false)); });
          state.shown += entries.length;
          state.total = (data && data.total) || state.shown;
          table.hidden = state.shown === 0;
          // "provisional" is only in the answer once the server sends it; without the key the section stays hidden.
          if (!append && data && Array.isArray(data.provisional)) {
            data.provisional.forEach(function (e) { settlingRows.appendChild(ladderRow(e, true)); });
            state.settled = data.provisional.length;
            settling.hidden = state.settled === 0;
          }
          if (state.shown === 0 && state.settled === 0) message('No ranked players yet this season.');
          else msg.hidden = true;
          moreWrap.hidden = !(state.total > state.shown && entries.length > 0);
          more.disabled = false;
        })
        .catch(function () {
          if (id !== state.request) return;
          more.disabled = false;
          if (append) message('Could not load more players.', true, true);
          else message('The ladder could not be loaded right now.', true, false);
        });
    }

    function select(tab, focus) {
      tabs.forEach(function (t) {
        var on = t === tab;
        t.setAttribute('aria-selected', on ? 'true' : 'false');
        t.tabIndex = on ? 0 : -1;
      });
      if (focus) tab.focus();
      panel.setAttribute('aria-labelledby', tab.id);
      state.mode = tab.getAttribute('data-mode');
      try { window.history.replaceState(null, '', '#' + state.mode); } catch (e) { /* old browser */ }
      load(false);
    }

    tabs.forEach(function (tab, i) {
      tab.addEventListener('click', function () { if (tab.getAttribute('aria-selected') !== 'true') select(tab, false); });
      tab.addEventListener('keydown', function (e) {
        var step = e.key === 'ArrowRight' ? 1 : e.key === 'ArrowLeft' ? -1 : 0;
        if (!step) return;
        e.preventDefault();
        select(tabs[(i + step + tabs.length) % tabs.length], true);
      });
    });
    more.addEventListener('click', function () { load(true); });

    loadSeason(base);
    var wanted = (window.location.hash || '').slice(1);
    var first = tabs.filter(function (t) { return t.getAttribute('data-mode') === wanted; })[0] || tabs[0];
    select(first, false);
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

  // ---- the trailer: a YouTube player (privacy-enhanced mode) made only on click ----
  function setupTrailer() {
    var play = document.getElementById('trailer-play');
    var box = document.getElementById('trailer-media');
    if (!play || !box) return;
    play.addEventListener('click', function (e) {
      var id = play.getAttribute('data-video');
      if (!/^[A-Za-z0-9_-]{11}$/.test(id || '')) return;
      e.preventDefault();
      var f = document.createElement('iframe');
      f.src = 'https://www.youtube-nocookie.com/embed/' + id + '?autoplay=1&rel=0';
      f.title = 'FFNY Online trailer';
      f.allow = 'autoplay; encrypted-media; picture-in-picture; fullscreen';
      f.allowFullscreen = true;
      // The page sends no referrer; YouTube's player refuses to start without one (error 153).
      f.referrerPolicy = 'strict-origin-when-cross-origin';
      box.textContent = '';
      box.appendChild(f);
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

  // ---- FFNY Studio community gallery (studio.html): the public, signed content catalogue ----
  // The same file the launcher reads; only the fields shown here are used, and nothing is sent but the GET.
  var CATALOGUE = 'https://play.defjam.online/updates/content/index-v3.json';
  var PACKS_SHOWN = 12;

  // A local preview may read a test catalogue next to the page: http://localhost:8000/studio.html?catalogue=test-40.json
  function catalogueUrl() {
    var host = window.location.hostname;
    var m = /[?&]catalogue=([\w.-]+\.json)(?:&|$)/.exec(window.location.search);
    return (m && (host === 'localhost' || host === '127.0.0.1')) ? m[1] : CATALOGUE;
  }

  var PACK_TYPES = {
    'new-fighter': ['Fighter', 'fighter'],
    'new-venue': ['Map', 'map'],
    'venue': ['Map', 'map'],
    'clothing': ['Clothing', 'clothing'],
    'voice': ['Voice', 'other'],
    'language': ['Language', 'other']
  };
  var PACK_ICONS = {
    fighter: 'M12 2.5a3.2 3.2 0 1 1 0 6.4 3.2 3.2 0 0 1 0-6.4ZM7.2 10.6h9.6l2.4 5.6-2 .8-1.6-3.4v8.1h-2.4v-5.2h-2.4v5.2H8.4v-8.1L6.8 17l-2-.8 2.4-5.6Z',
    map: 'M3 6.2 8.6 4l6.8 2.4L21 4.2v13.6l-5.6 2.2-6.8-2.4L3 19.8V6.2Zm6 .2v9.9l6 2.1V8.5L9 6.4Z',
    clothing: 'M8.5 3.5 12 5.6l3.5-2.1 5.5 3.2-2 4.3-2.2-1v10.5H7.2V10l-2.2 1-2-4.3 5.5-3.2Z',
    other: 'M12 3l2.6 5.6 6.1.7-4.5 4.2 1.2 6L12 16.6 6.6 19.5l1.2-6L3.3 9.3l6.1-.7L12 3Z'
  };

  function packType(type) {
    var known = PACK_TYPES[type];
    if (known) return { label: known[0], cls: known[1] };
    var label = String(type || 'Pack').replace(/^new-/, '').replace(/-/g, ' ');
    return { label: label.charAt(0).toUpperCase() + label.slice(1), cls: 'other' };
  }

  // A name made only of blank or unassigned characters reads as nothing; show a plain fallback instead.
  function visibleText(s) {
    s = typeof s === 'string' ? s.trim() : '';
    try {
      return new RegExp('[\\p{L}\\p{N}\\p{S}\\p{P}]', 'u').test(s) ? s : '';
    } catch (e) {
      return /\S/.test(s) ? s : '';
    }
  }

  function livePacks(catalogue) {
    var mods = (catalogue && catalogue.mods) || [];
    var out = [];
    mods.forEach(function (m) {
      if (!m || m.kind !== 'content') return;
      var revoked = m.revoked_versions || [];
      var versions = (m.versions || []).filter(function (v) {
        return v && v.channel !== 'beta' && revoked.indexOf(v.version) < 0;
      });
      if (!versions.length) return;
      var v = versions[versions.length - 1];
      var authors = (m.authors || []).map(visibleText).filter(Boolean);
      var pic = typeof m.picture === 'string' ? m.picture : (typeof m.image === 'string' ? m.image : '');
      out.push({
        id: String(m.id || ''),
        name: visibleText(m.name) || 'Untitled pack',
        authors: authors,
        type: packType(m.type),
        version: String(v.version || ''),
        reviewed: v.reviewed || '',
        // only pictures from the content service itself (the site's CSP must allow it in img-src)
        picture: /^https:\/\/play\.defjam\.online\//.test(pic) ? pic : '',
        order: typeof m.order === 'number' ? m.order : 1e9
      });
    });
    out.sort(function (a, b) { return (b.reviewed > a.reviewed ? 1 : b.reviewed < a.reviewed ? -1 : a.order - b.order); });
    return out;
  }

  function svgIcon(path) {
    var ns = 'http://www.w3.org/2000/svg';
    var svg = document.createElementNS(ns, 'svg');
    svg.setAttribute('viewBox', '0 0 24 24');
    svg.setAttribute('aria-hidden', 'true');
    var p = document.createElementNS(ns, 'path');
    p.setAttribute('d', path);
    p.setAttribute('fill', 'currentColor');
    svg.appendChild(p);
    return svg;
  }

  function packCard(p, single) {
    var li = el('li');
    var card = el('article', 'glass pack');
    var art = el('div', 'pack-art t-' + p.type.cls);
    if (p.picture) {
      var img = el('img');
      img.src = p.picture;
      img.alt = '';
      img.loading = 'lazy';
      img.decoding = 'async';
      art.appendChild(img);
    } else {
      art.appendChild(svgIcon(PACK_ICONS[p.type.cls] || PACK_ICONS.other));
      art.appendChild(el('span', 'pack-initial', p.name.charAt(0)));
    }
    var reviewed = p.reviewed ? new Date(p.reviewed + 'T00:00:00Z') : null;
    if (reviewed && !isNaN(reviewed) && Date.now() - reviewed.getTime() < 14 * 864e5) {
      art.appendChild(el('span', 'tag-new pack-new', 'New'));
    }
    card.appendChild(art);
    var info = el('div', 'pack-info');
    info.appendChild(el('span', 'pack-type', p.type.label));
    info.appendChild(el('h3', null, p.name));
    info.appendChild(el('p', 'pack-by', 'by ' + (p.authors.length ? p.authors.join(', ') : 'a community creator')));
    var meta = [];
    if (p.version) meta.push('v' + p.version);
    if (p.reviewed) meta.push('live since ' + shortDate(p.reviewed + 'T00:00:00Z'));
    if (meta.length) info.appendChild(el('p', 'pack-meta', meta.join(' · ')));
    if (single) {
      info.appendChild(el('p', 'pack-hint', 'Installed for every player by the launcher. Pick it in any room.'));
    }
    card.appendChild(info);
    li.appendChild(card);
    return li;
  }

  function setupStudioGallery() {
    var msg = document.getElementById('gallery-msg');
    var grid = document.getElementById('pack-grid');
    var count = document.getElementById('gallery-count');
    var filter = document.getElementById('pack-filter');
    var moreWrap = document.getElementById('pack-more-wrap');
    var more = document.getElementById('pack-more');
    if (!msg || !grid) return;
    if (!window.fetch) { msg.hidden = true; return; }
    var packs = [];
    var wanted = '';
    var showAll = false;

    function say(text, retry) {
      msg.textContent = '';
      var p = el('p', 'ladder-msg', text);
      if (retry) {
        var b = el('button', null, 'Try again');
        b.type = 'button';
        b.addEventListener('click', load);
        p.appendChild(b);
      }
      msg.appendChild(p);
      msg.hidden = false;
    }

    function render() {
      var list = packs.filter(function (p) { return !wanted || p.type.label === wanted; });
      var shown = showAll ? list : list.slice(0, PACKS_SHOWN);
      grid.textContent = '';
      var single = packs.length === 1;
      grid.classList.toggle('single', single);
      shown.forEach(function (p) { grid.appendChild(packCard(p, single)); });
      grid.hidden = false;
      moreWrap.hidden = shown.length >= list.length;
      more.textContent = 'Show all ' + list.length;
    }

    function buildFilter() {
      var labels = [];
      packs.forEach(function (p) { if (labels.indexOf(p.type.label) < 0) labels.push(p.type.label); });
      filter.textContent = '';
      if (labels.length < 2 || packs.length < 6) { filter.hidden = true; return; }
      [''].concat(labels).forEach(function (label) {
        var b = el('button', null, label || 'All');
        b.type = 'button';
        b.setAttribute('aria-pressed', label === wanted ? 'true' : 'false');
        b.addEventListener('click', function () {
          wanted = label;
          showAll = false;
          Array.prototype.forEach.call(filter.children, function (c) { c.setAttribute('aria-pressed', c === b ? 'true' : 'false'); });
          render();
        });
        filter.appendChild(b);
      });
      filter.hidden = false;
    }

    function load() {
      say('Loading the community packs…');
      fetch(catalogueUrl(), { credentials: 'omit', referrerPolicy: 'no-referrer' })
        .then(function (r) { if (!r.ok) throw new Error('catalogue ' + r.status); return r.json(); })
        .then(function (data) {
          packs = livePacks(data);
          if (!packs.length) {
            grid.hidden = true;
            count.hidden = true;
            say('No community packs are live yet. Yours could be the first: see how to get access below.');
            return;
          }
          msg.hidden = true;
          count.textContent = '';
          count.appendChild(el('b', null, String(packs.length)));
          count.appendChild(document.createTextNode(packs.length === 1 ? ' pack live' : ' packs live'));
          count.hidden = false;
          buildFilter();
          render();
        })
        .catch(function () {
          grid.hidden = true;
          say('The community packs could not be loaded right now. The launcher still installs every live pack for you.', true);
        });
    }

    more.addEventListener('click', function () { showAll = true; render(); });
    load();
  }

  function start() {
    if (document.getElementById('ladder-panel')) { setupLadder(); return; }
    if (document.getElementById('turf-body')) { setupTurf(); return; }
    if (document.getElementById('pack-grid')) { setupStudioGallery(); setupLightbox(); return; }
    loadRelease();
    loadTournaments();
    loadTurfTeaser();
    setupLightbox();
    setupClip();
    setupTrailer();
    setupEmbers();
  }

  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start);
  else start();
})();
