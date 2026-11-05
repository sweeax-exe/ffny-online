// Def Jam: Fight for NY Fan Recomp site: latest release, OS-aware download, news, sliders, lightbox.
// The page works without this script; it only fills in live details.
(function () {
  'use strict';

  var REPO = 'vyanhursky/DefJamFFNY-recomp';
  var API = 'https://api.github.com/repos/' + REPO + '/releases?per_page=10';
  var NEWS_COUNT = 4;

  var PLATFORMS = {
    windows: { label: 'Download for Windows', sub: 'x64', asset: 'windows-x64.exe' },
    mac: { label: 'Download for macOS', sub: 'Apple Silicon', asset: 'macos-arm64.dmg' },
    linux: { label: 'Download for Linux', sub: 'x86-64 & Steam Deck', asset: 'linux-x64.tar.gz' }
  };

  // Since v0.13.0 a release carries the app (DefJamRecompiled-<v>-<platform>, D215), which sets up,
  // launches and updates the game, beside the setup (DefJamSetup-<v>-<platform>). A button asks for
  // its platform's setup suffix; the app is offered where the release has one, the setup otherwise
  // (older releases, Linux). Names are matched whole, so the two Windows .exe files never mix.
  var APP_FOR = { 'windows-x64.exe': 'windows-x64.exe', 'macos-arm64.dmg': 'macos-arm64.zip' };

  // Best guess at the visitor's desktop OS; null on phones, tablets and anything unknown.
  function detectOS() {
    var ua = navigator.userAgent || '';
    var platform = (navigator.userAgentData && navigator.userAgentData.platform) || navigator.platform || '';
    if (/Android|iPhone|iPad|iPod/i.test(ua)) return null;
    if (/win/i.test(platform) || /Windows/.test(ua)) return 'windows';
    if (/mac/i.test(platform) || /Macintosh/.test(ua)) {
      return navigator.maxTouchPoints > 1 ? null : 'mac'; // iPadOS reports itself as a Mac
    }
    if (/linux|x11|cros/i.test(platform) || /Linux|X11|CrOS/.test(ua)) return 'linux';
    return null;
  }

  function el(tag, cls, text) {
    var node = document.createElement(tag);
    if (cls) node.className = cls;
    if (text != null) node.textContent = text;
    return node;
  }

  function formatDate(iso) {
    var d = new Date(iso);
    if (isNaN(d)) return '';
    return d.toLocaleDateString('en-US', { year: 'numeric', month: 'short', day: 'numeric' });
  }

  function plain(md) {
    return md
      .replace(/!\[[^\]]*\]\([^)]*\)/g, '')
      .replace(/\[([^\]]+)\]\([^)]*\)/g, '$1')
      .replace(/[*_`]+/g, '')
      .replace(/\s+/g, ' ')
      .trim();
  }

  // First prose paragraph of a release body, without Markdown. A paragraph that introduces a
  // list ("focuses on two things:") is finished with the bold lead of each list item.
  function summary(body) {
    var blocks = String(body || '').replace(/\r/g, '').split(/\n\s*\n/);
    for (var i = 0; i < blocks.length; i++) {
      var b = blocks[i].trim();
      if (!b || /^(#|[-*+] |\d+\. |\||```|>|<)/.test(b)) continue;
      var text = plain(b);
      var next = (blocks[i + 1] || '').trim();
      if (/:$/.test(text) && /^[-*+] /.test(next)) {
        var leads = next.split(/\n(?=[-*+] )/).map(function (item) {
          var bold = item.match(/\*\*([^*]+)\*\*/);
          return plain(bold ? bold[1] : item.replace(/^[-*+] /, '')).replace(/[.:]$/, '');
        });
        var last = leads.pop();
        text += ' ' + (leads.length ? leads.join(', ') + ' and ' + last : last).toLowerCase() + '.';
      }
      if (text.length > 320) text = text.slice(0, 317).replace(/\s+\S*$/, '') + '…';
      return text;
    }
    return '';
  }

  // The body's own "# v0.12.1 — Created fighters…" heading names the release better than its title,
  // which is sometimes just the product name and version.
  function title(release) {
    var heading = String(release.body || '').match(/^#\s+(.+)$/m);
    var name = plain(heading ? heading[1] : (release.name || release.tag_name));
    var words = name.replace(/^v?\d+(\.\d+)+\s+[—–-]\s+/, '');
    return words || name;
  }

  function findAsset(release, prefix, suffix) {
    var assets = (release && release.assets) || [];
    var pattern = new RegExp('^' + prefix + '-\\d+\\.\\d+\\.\\d+-' + suffix.replace(/\./g, '\\.') + '$');
    for (var i = 0; i < assets.length; i++) {
      if (pattern.test(assets[i].name)) return assets[i];
    }
    return null;
  }

  // The asset a button for `suffix` should download: the app when the release has it, else the setup.
  function pickAsset(release, suffix) {
    var app = APP_FOR[suffix] && findAsset(release, 'DefJamRecompiled', APP_FOR[suffix]);
    return app || findAsset(release, 'DefJamSetup', suffix);
  }

  function assetUrl(release, suffix) {
    var asset = pickAsset(release, suffix);
    return asset ? asset.browser_download_url : null;
  }

  function extension(asset) {
    var match = asset && asset.name.match(/\.(tar\.gz|[a-z]+)$/);
    return match ? '.' + match[1] : '';
  }

  function applyDownloads(os, latest) {
    if (latest) {
      document.querySelectorAll('[data-asset]').forEach(function (a) {
        var asset = pickAsset(latest, a.getAttribute('data-asset'));
        if (!asset) return;
        a.href = asset.browser_download_url;
        // A card's "Download .dmg" names the file it now gets (the app's .zip on a Mac).
        if (/^Download \.[a-z.]+$/.test(a.textContent.trim())) a.textContent = 'Download ' + extension(asset);
      });
    }
    if (!os) return;
    var p = PLATFORMS[os];
    var primary = document.getElementById('download-primary');
    primary.querySelector('.btn__label').textContent = p.label;
    var asset = latest && pickAsset(latest, p.asset);
    var format = asset ? extension(asset) : '.' + p.asset.split('.').slice(1).join('.');
    document.getElementById('download-sub').textContent =
      (latest ? latest.tag_name + ' · ' : '') + p.sub + ' · ' + format;
    if (asset) primary.href = asset.browser_download_url;
    var card = document.querySelector('.platform[data-platform="' + os + '"]');
    if (card) card.classList.add('is-yours');
  }

  function renderReleaseLine(latest) {
    var line = document.getElementById('release-line');
    line.textContent = '';
    line.appendChild(el('span', null, 'Latest: '));
    line.appendChild(el('span', 'tag', latest.tag_name));
    line.appendChild(document.createTextNode(' · ' + formatDate(latest.published_at) + ' · '));
    var a = el('a', null, 'Release notes');
    a.href = latest.html_url;
    line.appendChild(a);
  }

  function renderNews(releases) {
    var list = document.getElementById('news-list');
    list.textContent = '';
    releases.slice(0, NEWS_COUNT).forEach(function (r, i) {
      var post = el('article', 'post');
      var meta = el('div', 'post__meta');
      meta.appendChild(el('span', 'tag', r.tag_name));
      meta.appendChild(document.createTextNode(formatDate(r.published_at)));
      var body = el('div');
      var h = el('h3');
      var link = el('a', null, title(r));
      link.href = r.html_url;
      h.appendChild(link);
      if (i === 0) h.appendChild(el('span', 'post__latest', 'Latest'));
      body.appendChild(h);
      var text = summary(r.body);
      if (text) body.appendChild(el('p', null, text));
      post.appendChild(meta);
      post.appendChild(body);
      list.appendChild(post);
    });
  }

  function getJSON(url) {
    return fetch(url, { headers: { Accept: 'application/vnd.github+json' } }).then(function (res) {
      if (!res.ok) throw new Error(url + ': HTTP ' + res.status);
      return res.json();
    });
  }

  // Live from GitHub first; the copy baked in at deploy time covers rate limits and outages.
  function loadReleases() {
    return getJSON(API).catch(function () { return getJSON('releases.json'); }).then(function (all) {
      return (Array.isArray(all) ? all : []).filter(function (r) { return !r.draft && !r.prerelease; });
    });
  }

  function initSliders() {
    document.querySelectorAll('[data-compare]').forEach(function (fig) {
      var stage = fig.querySelector('.compare__stage');
      var range = fig.querySelector('.compare__range');
      var set = function () { stage.style.setProperty('--pos', range.value + '%'); };
      range.addEventListener('input', set);
      set();
    });
  }

  function initLightbox() {
    var box = document.getElementById('lightbox');
    if (!box || typeof box.showModal !== 'function') return;
    var img = box.querySelector('img');
    document.querySelectorAll('[data-full]').forEach(function (btn) {
      btn.addEventListener('click', function () {
        var thumb = btn.querySelector('img');
        img.src = btn.getAttribute('data-full');
        img.alt = thumb ? thumb.alt : '';
        box.showModal();
      });
    });
    box.addEventListener('click', function (e) { if (e.target === box) box.close(); });
  }

  var os = detectOS();
  applyDownloads(os, null);
  initSliders();
  initLightbox();

  loadReleases().then(function (releases) {
    if (!releases.length) return;
    renderReleaseLine(releases[0]);
    applyDownloads(os, releases[0]);
    renderNews(releases);
  }).catch(function () { /* Static links already point at the releases page. */ });
})();
