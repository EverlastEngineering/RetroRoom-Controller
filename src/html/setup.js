R""""(
(function () {
  const $ = (id) => document.getElementById(id);
  const ssidSel   = $('ssid');
  const pass      = $('pass');
  const passWrap  = $('passwrap');
  const passHint  = $('passhint');
  const save      = $('save');
  const status    = $('status');
  const spin      = $('spin');
  const rescan    = $('rescan');
  const manual    = $('manual');
  const manualInp = $('ssid_manual');

  function setStatus(msg)  { status.textContent = msg || ''; }
  function setBusy(busy)  {
    // Note: do NOT toggle ssidSel.disabled here. The submit handler
    // runs setBusy(true) right before the browser collects form data,
    // and a disabled select is stripped from the POST. We only want
    // to block UI interaction (button, spinner, rescan link), not
    // mutate form-field state.
    save.disabled = busy;
    spin.classList.toggle('hidden', !busy);
    rescan.style.pointerEvents = busy ? 'none' : '';
    if (busy) {
      manualInp.disabled = true;
    } else {
      manualInp.disabled = false;
    }
  }

  function isOpen(opt) {
    return opt && opt.dataset && opt.dataset.open === '1';
  }

  function updatePassUI() {
    const opt = ssidSel.options[ssidSel.selectedIndex];
    if (isOpen(opt)) {
      passWrap.classList.add('hidden');
      pass.value = '';
      pass.removeAttribute('required');
    } else {
      passWrap.classList.remove('hidden');
      pass.setAttribute('required', '');
      passHint.textContent = opt && opt.dataset.auth
        ? 'Authentication: ' + opt.dataset.auth + '.'
        : 'Required for secured networks.';
    }
  }

  function renderPlaceholder(label) {
    ssidSel.innerHTML = '';
    const o = document.createElement('option');
    o.value = ''; o.textContent = label;
    ssidSel.appendChild(o);
  }

  async function load() {
    setBusy(true);
    renderPlaceholder('Scanning\u2026');
    setStatus('');
    try {
      const r = await fetch('/scan.json', { cache: 'no-store' });
      if (!r.ok) throw new Error('HTTP ' + r.status);
      const obj  = await r.json();
      const nets = obj && obj.networks;
      ssidSel.innerHTML = '';
      if (!Array.isArray(nets) || nets.length === 0) {
        const why = obj ? ('(scan returned ' + obj._count + ')') : '(no JSON)';
        renderPlaceholder('No networks found ' + why);
        manual.classList.remove('hidden');
      } else {
        nets.sort((a, b) => (b.rssi | 0) - (a.rssi | 0));
        for (const n of nets) {
          const o = document.createElement('option');
          o.value = n.ssid || '';
          o.textContent = (n.open ? '[ ] ' : '[*] ')
            + (n.ssid || '(hidden)')
            + '  (' + n.rssi + ' dBm, ch ' + n.channel + ')';
          o.dataset.open  = n.open ? '1' : '0';
          o.dataset.auth  = n.auth || '';
          o.dataset.rssi  = String(n.rssi);
          ssidSel.appendChild(o);
        }
        manual.classList.remove('hidden');
      }
      setBusy(false);
      updatePassUI();
    } catch (e) {
      renderPlaceholder('Scan failed');
      setStatus('Scan failed: ' + e.message + '. You can still type an SSID below.');
      manual.classList.remove('hidden');
      setBusy(false);
      updatePassUI();
    }
  }

  ssidSel.addEventListener('change', updatePassUI);
  rescan.addEventListener('click', (e) => { e.preventDefault(); load(); });

  manualInp.addEventListener('input', () => {
    const v = manualInp.value.trim();
    if (!v) return;
    let opt = Array.from(ssidSel.options).find((o) => o.value === v);
    if (!opt) {
      opt = document.createElement('option');
      opt.value = v;
      opt.textContent = '[?] ' + v + '  (manual)';
      opt.dataset.open = '0';
      opt.dataset.auth = 'unknown';
      ssidSel.insertBefore(opt, ssidSel.firstChild);
    }
    ssidSel.value = v;
    updatePassUI();
  });

  document.getElementById('f').addEventListener('submit', () => {
    setBusy(true);
    save.textContent = 'Saving\u2026';
    setStatus('Saving credentials and rebooting\u2026');
  });

  load();
})();
)""""