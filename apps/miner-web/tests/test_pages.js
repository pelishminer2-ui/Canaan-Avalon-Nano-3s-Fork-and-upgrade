// SPDX-License-Identifier: MIT
/*
 * Copyright (c) 2026, Canaan Creative Co., Ltd.
 *
 * For the licensing terms, see the LICENSE file (MIT License).
 *
 * NO WARRANTY. The Software is provided "AS IS", without warranty of any kind, express or
 * implied, including but not limited to the warranties of merchantability, fitness for a
 * particular purpose and non-infringement. In no event shall the authors or copyright
 * holders be liable for any claim, damages or other liability arising from, out of or in
 * connection with the Software or the use or other dealings in the Software.
 */

const fs = require('fs');
const vm = require('vm');
const assert = require('assert');
const path = require('path');
function page(name) {
    const calls = [], alerts = [], timers = [], qrs = [], nodes = new Map();
    const classList = () => {
        const s = new Set();
        return {
            add: c => s.add(c),
            remove: c => s.delete(c),
            toggle: (c, on) => on ? s.add(c) : s.delete(c),
            contains: c => s.has(c)
        };
    };
    const element = extra => ({
        value: '',
        style: {},
        classList: classList(),
        children: [],
        removeChild() {},
        addEventListener() {},
        setAttribute(k, v) {
            this[k] = v;
        },
        appendChild(c) {
            this.children.push(c);
        },
        get innerHTML() {
            return this._html;
        },
        set innerHTML(v) {
            this._html = v;
            if (v === '') this.children = [];
        },
        ...extra
    });
    const node = key => {
        if (!nodes.has(key)) nodes.set(key, element());
        return nodes.get(key);
    };
    const form = {method: 'post', submit() {}};
    for (let i = 1; i <= 3; i++)
        for (const prefix of ['pool', 'worker', 'passwd'])
            form[prefix + i] = {value: ''};
    const ctx = {
        console, TextEncoder, URLSearchParams, Date, Math, JSON,
        document: {
            querySelector: node,
            getElementById: node,
            createElement: () => element(),
            cookie: ''
        },
        cgpoolform: form, rebootform: {method: 'post'}, dashform: {submit() {}},
        loginform: {style: {}, password: element({focus() {}}), submit() {}},
        location: {
            reload() {
                calls.push({reload: true});
            }
        },
        alert: msg => alerts.push(msg),
        setInterval: fn => {
            timers.push(fn);
            return timers.length;
        },
        clearInterval() {},
        setTimeout: fn => {
            timers.push(fn);
            return timers.length;
        },
        clearTimeout: id => {
            if (id) timers[id - 1] = () => {};
        },
        fetch: (url, options) => {
            calls.push({url, options});
            return Promise.resolve({ok: true, json: () => Promise.resolve({activated: 1})});
        },
        FormData: class {
            constructor(f) {
                this.f = f;
            }
            *entries() {
                for (const [k, v] of Object.entries(this.f))
                    if (v && typeof v === 'object' && 'value' in v)
                        yield [k, v.value];
            }
        },
        QRCode: class {
            constructor(el, opts) {
                qrs.push(JSON.parse(opts.text));
            }
        },
        XMLHttpRequest: class {
            open(method, url) {
                this.method = method;
                this.url = url;
            }
            setRequestHeader() {}
            send(body) {
                calls.push({method: this.method, url: this.url, body, xhr: this});
            }
        }
    };
    ctx.window = ctx;
    ctx.QRCode.CorrectLevel = {H: 1};
    vm.createContext(ctx);
    const html = fs.readFileSync(path.join(__dirname, '../assets', name + '.html'), 'utf8');
    for (const m of html.matchAll(/<script\b[^>]*>([\s\S]*?)<\/script>/gi))
        if (m[1].trim()) vm.runInContext(m[1], ctx);
    return {ctx, calls, alerts, timers, qrs, form};
}
const flush = async () => {
    for (let i = 0; i < 8; i++) await Promise.resolve();
};
(async () => {
    const p = page('pools');
    p.calls.length = 0;
    const confirms = [];
    let restart = false;
    p.ctx.confirm = m => {
        confirms.push(m);
        return restart;
    };
    p.form.pool1.value = 'stratum+tcp://pool:3333';
    p.form.worker1.value = 'w'.repeat(128);
    p.ctx.submit();
    await flush();
    assert.equal(p.calls.length, 0);
    assert(p.alerts.some(x => x.includes('cannot be empty')));
    p.form.passwd1.value = 'x';
    p.ctx.submit();
    await flush();
    assert.equal(p.calls.length, 1);
    assert(p.calls[0].options.body.includes('passwd1=x'));
    assert.equal(p.calls[0].url, '/api/v1/pools');
    assert.equal(p.calls[0].options.method, 'post');
    assert.equal(p.calls[0].options.headers['Content-Type'], 'application/x-www-form-urlencoded');
    assert(!p.alerts.some(x => x.includes('restart')));
    assert.equal(confirms.length, 1);
    restart = true;
    p.calls.length = 0;
    p.ctx.submit();
    await flush();
    assert.equal(confirms.length, 2);
    assert.deepEqual(p.calls.map(c => c.url), ['/api/v1/pools', '/api/v1/system/reboot']);
    restart = false;
    const savedFetch = p.ctx.fetch;
    p.ctx.fetch = () => Promise.resolve({
        ok: false,
        json: () => Promise.resolve({reason: 'bad pool'})
    });
    p.ctx.submit();
    await flush();
    assert.equal(confirms.length, 2);
    assert.equal(p.alerts.at(-1), 'bad pool');
    p.ctx.fetch = savedFetch;
    for (const bad of ['stratum+tls://pool:3333', 'stratum+ssl://pool:3333']) {
        p.calls.length = 0;
        p.form.pool1.value = bad;
        p.ctx.submit();
        await flush();
        assert.equal(p.calls.length, 0);
        assert(p.alerts.at(-1).includes('not supported'));
    }
    for (const bad of ['http://pool:3333', 'pool:3333']) {
        p.calls.length = 0;
        p.form.pool1.value = bad;
        p.ctx.submit();
        await flush();
        assert.equal(p.calls.length, 0);
        assert.equal(p.alerts.at(-1), 'Pool address must start with stratum+tcp://');
    }
    p.calls.length = 0;
    p.form.pool1.value = 'stratum+tcp://pool:3333';
    p.calls.length = 0;
    p.form.worker1.value = 'w'.repeat(129);
    p.ctx.submit();
    assert.equal(p.calls.length, 0);
    assert(p.alerts.some(x => x.includes('128 bytes')));
    const url256 = 'stratum+tcp://' + 'h'.repeat(256 - 'stratum+tcp://'.length);
    p.form.worker1.value = 'w';
    p.form.pool1.value = url256;
    p.ctx.submit();
    await flush();
    assert.equal(p.calls.length, 1);
    p.calls.length = 0;
    p.form.pool1.value = url256 + 'h';
    p.ctx.submit();
    assert.equal(p.calls.length, 0);
    assert(p.alerts.some(x => x.includes('256 bytes')));
    p.ctx.fetch = () => Promise.resolve({ok: false});
    const before = p.timers.length;
    p.ctx.reboot();
    await flush();
    assert.equal(p.timers.length, before);
    assert(p.alerts.some(x => x.includes('reboot')));
    const kp = page('pools');
    kp.ctx.confirm = () => false;
    const pass1 = kp.ctx.document.querySelector('.pool-panel1 .input-password');
    const pass2 = kp.ctx.document.querySelector('.pool-panel2 .input-password');
    kp.ctx.dashboardCallback({hwtype: 'Avalon Nano3s', sys_status: '1',
        pool1: 'stratum+tcp://pool:3333', worker1: 'w', passwd1: 'saved1', pool2: '', worker2: '',
        passwd2: '', pool3: '', worker3: '', passwd3: ''});
    assert.equal(pass1.value, 'saved1');
    assert.equal(pass2.value, '');
    assert(pass1.placeholder.includes('leave blank'));
    assert.equal(pass2.placeholder, 'password');
    kp.calls.length = 0;
    kp.form.pool1.value = 'stratum+tcp://pool:3333';
    kp.form.worker1.value = 'w';
    kp.form.pool2.value = 'stratum+tcp://new:3333';
    kp.form.worker2.value = 'w2';
    kp.ctx.submit();
    await flush();
    assert.equal(kp.calls.length, 0);
    assert.equal(kp.alerts.at(-1), 'Pool #2 configuration options cannot be empty');
    kp.form.pool2.value = '';
    kp.form.worker2.value = '';
    kp.ctx.submit();
    await flush();
    assert.equal(kp.calls.length, 1);
    assert(kp.calls[0].options.body.includes('passwd1=&'));
    const mp = page('pools');
    const mpass = [1, 2, 3].map(
        i => mp.ctx.document.querySelector(`.pool-panel${i} .input-password`));
    mp.ctx.dashboardCallback({hwtype: 'Avalon Nano3s', sys_status: '1',
        pool1: 'stratum+tcp://pool:3333', worker1: 'w', passwd1: '*', pool2: 'stratum+tcp://b:3333',
        worker2: 'w2', passwd2: '*', pool3: '', worker3: '', passwd3: ''});
    assert.deepEqual(mpass.map(e => e.value), ['', '', '']);
    assert(mpass[0].placeholder.includes('leave blank'));
    assert(mpass[1].placeholder.includes('leave blank'));
    assert.equal(mpass[2].placeholder, 'password');
    const poolsHtml = fs.readFileSync(path.join(__dirname, '../assets/pools.html'), 'utf8');
    for (let i = 1; i <= 3; i++) {
        const m = poolsHtml.match(new RegExp(`<input\\b[^>]*name="passwd${i}"[^>]*>`));
        assert(m && /type="password"/.test(m[0]) && /autocomplete="new-password"/.test(m[0]), i);
    }
    const a = page('qr_login'), b = page('qr_login');
    a.ctx.getAuthCallback({auth: '11111111', code: 'dna', session: 'a'.repeat(32)});
    b.ctx.getAuthCallback({auth: '22222222', code: 'dna', session: 'b'.repeat(32)});
    assert(!('session' in a.qrs[0]));
    a.ctx.checkLogin();
    b.ctx.checkLogin();
    assert.equal(a.calls.at(-1).body, 'session=' + 'a'.repeat(32));
    assert.equal(b.calls.at(-1).body, 'session=' + 'b'.repeat(32));
    assert.equal(a.calls.at(-1).method, 'POST');
    const qrCookie = a.ctx.document.cookie;
    a.ctx.getCookieCallback({auth: '11111111'});
    assert.equal(a.ctx.document.cookie, qrCookie);
    assert.equal(a.ctx.location.href, 'dashboard.html');
    assert.equal(a.ctx.setCookie, undefined);
    const l = page('login');
    l.ctx.getAuthCallback({auth: '11111111', code: 'dna', activated: 0});
    l.timers.at(-1)();
    await flush();
    assert(l.calls.some(x => x.reload));
    assert.equal(l.ctx.document.getElementById('qrblock').style.display, '');
    assert.equal(l.ctx.document.getElementById('startblock').style.display, 'none');
    const html = fs.readFileSync(path.join(__dirname, '../assets/login.html'), 'utf8');
    assert(/<div id="startblock"[^>]*>Starting\.\.\.<\/div>/.test(html) &&
           !/id="startblock"[^>]*display: none/.test(html));
    assert(/<div id="qrblock" style="display: none;">/.test(html));
    const st0 = page('login');
    let startState = {activated: 0, starting: 1};
    st0.ctx.fetch = () => Promise.resolve({ok: true, json: () => Promise.resolve(startState)});
    st0.ctx.getAuthCallback({auth: '11111111', code: 'dna', activated: 0, starting: 1});
    const sdoc = st0.ctx.document;
    assert.equal(sdoc.getElementById('startblock').style.display, undefined);
    assert.equal(sdoc.getElementById('qrblock').style.display, undefined);
    assert.equal(st0.ctx.loginform.style.display, undefined);
    const startPoll = st0.timers.at(-1);
    startPoll();
    await flush();
    assert(!st0.calls.some(x => x.reload));
    startState = {activated: 0};
    startPoll();
    await flush();
    assert(st0.calls.some(x => x.reload));
    const st1 = page('login');
    st1.ctx.getAuthCallback({auth: '11111111', code: 'dna', activated: 1});
    assert.equal(st1.ctx.document.getElementById('startblock').style.display, 'none');
    assert.equal(st1.ctx.loginform.style.display, '');
    const submitEvent = () => ({
        prevented: false,
        preventDefault() {
            this.prevented = true;
        }
    });
    const login = (nonce) => {
        const g = page('login'), ev = submitEvent();
        g.ctx.sha256 = () => 'ab'.repeat(32);
        if (nonce) g.ctx.getAuthCallback({auth: nonce, code: 'dna', activated: 1});
        g.ctx.loginform.password.value = 'pw';
        g.ctx.checkLogin(ev);
        const q = k => g.ctx.document.querySelector(k);
        return {
            g,
            ev,
            x: g.calls.find(c => c.url === '/api/v1/session'),
            err: q('.login-error'),
            btn: q('#login-button'),
            pw: g.ctx.loginform.password,
            wrap: q('.password-wrapper')
        };
    };
    let t = login('11111111');
    assert(t.ev.prevented);
    assert.equal(t.x.method, 'POST');
    assert.equal(t.x.body, 'passwd=11111111' + 'ab'.repeat(12));
    assert(t.btn.disabled);
    assert.equal(t.btn.textContent, 'Logging in…');
    assert(t.x.xhr.timeout > 0);
    assert.equal(t.pw.value, '');
    assert(t.g.ctx.document.cookie.startsWith('auth=11111111' + 'ab'.repeat(12) + '; expires='));
    assert(t.g.ctx.document.cookie.endsWith('; path=/; SameSite=Strict'));
    t.g.ctx.location.protocol = 'https:';
    t.g.ctx.setCookie('auth', 'x', 1);
    assert(t.g.ctx.document.cookie.endsWith('; path=/; SameSite=Strict; Secure'));
    t.g.ctx.clearCookie('auth');
    assert(t.g.ctx.document.cookie.endsWith('; path=/; Secure;'));
    delete t.g.ctx.location.protocol;
    t.x.xhr.status = 401;
    t.x.xhr.onload();
    assert(t.err.textContent.includes('Incorrect password'));
    assert(!t.btn.disabled);
    assert.equal(t.btn.textContent, 'Login');
    assert(t.pw.classList.contains('input-error'));
    assert.equal(t.pw['aria-invalid'], 'true');
    assert(t.wrap.classList.contains('shake'));
    assert(!t.g.ctx.location.href);
    assert(t.g.ctx.document.cookie.includes('1970'));
    t.g.ctx.showLoginError('');
    assert.equal(t.err.textContent, '');
    assert(!t.pw.classList.contains('input-error'));
    assert.equal(t.pw['aria-invalid'], 'false');
    t.pw.value = 'pw';
    t.g.ctx.checkLogin(submitEvent());
    assert.equal(t.g.calls.filter(c => c.url === '/api/v1/session').length, 2);
    const notPassword = r => {
        assert(!r.pw.classList.contains('input-error'));
        assert(r.wrap.classList.contains('shake'));
    };
    t = login('11111111');
    t.x.xhr.status = 403;
    t.x.xhr.onload();
    assert(t.err.textContent.includes('not activated'));
    notPassword(t);
    t = login('11111111');
    t.x.xhr.status = 413;
    t.x.xhr.onload();
    assert(t.err.textContent.includes('HTTP 413'));
    notPassword(t);
    t = login('11111111');
    t.x.xhr.onerror();
    assert(t.err.textContent.includes('did not reach'));
    assert(!t.btn.disabled);
    notPassword(t);
    t = login('11111111');
    t.x.xhr.ontimeout();
    assert(t.err.textContent.includes('did not answer'));
    assert(!t.btn.disabled);
    notPassword(t);
    t = login('11111111');
    t.x.xhr.status = 200;
    t.x.xhr.onload();
    assert.equal(t.g.ctx.location.href, 'dashboard.html');
    assert(t.btn.disabled);
    t = login('');
    assert(t.ev.prevented);
    assert(!t.x);
    assert(t.err.textContent.includes('Connecting'));
    notPassword(t);
    const e = page('login'), ev = submitEvent();
    e.ctx.loginform.password.value = '';
    e.ctx.checkLogin(ev);
    assert(ev.prevented);
    assert(e.alerts.includes('Password is null!'));
    const s = page('login'), sev = submitEvent();
    s.ctx.getAuthCallback({auth: '11111111', code: 'dna', activated: 1});
    s.ctx.loginform.password.value = 'pw';
    assert.throws(() => s.ctx.checkLogin(sev), /sha256/);
    assert(sev.prevented);
    assert(!s.calls.some(c => c.url === '/api/v1/session'));
    assert(!s.ctx.document.querySelector('#login-button').disabled);
    const d = page('dashboard');
    // telemetry unavailable: the callback is handed {} and must not throw on a
    // missing hwtype/version
    assert.doesNotThrow(() => d.ctx.dashboardCallback({}));
    d.ctx.dashboardCallback({hwtype: 'Avalon Nano3s', sys_status: '1', version: '1.2.3_abc'});
    assert.equal(d.ctx.document.querySelector('.header-name').innerHTML, 'Avalon Nano 3S');
    const markup = '<img src=x onerror=alert(1)>';
    d.ctx.dashboardCallback({hwtype: 'Avalon Nano3s', version: '1.2.3_abc', pool_status: '1',
        current_pool: markup, address: markup, worker: markup});
    const dq = k => d.ctx.document.querySelector(k);
    for (const k of ['.card-head-sub-title span', '.card-pool-address-text',
                     '.card-pool-worker-text']) {
        assert.equal(dq(k).textContent, markup);
        assert.equal(dq(k).innerHTML, undefined);
    }
    const st = dq('.card-head-sub-title span').children.at(-1);
    assert.equal(st.textContent, 'Active');
    assert.equal(st.style.marginLeft, '100px');
    assert.equal(st.style.color, '#000000');
    assert.equal(d.ctx.elapsed_obj, undefined);
    assert.equal(d.ctx.ajaxRequest, undefined);
    d.ctx.confirm = () => true;
    d.ctx.fetch = () => Promise.resolve({ok: false});
    const dbefore = d.timers.length;
    d.ctx.reboot();
    await flush();
    assert.equal(d.timers.length, dbefore);
    assert(d.alerts.some(x => x.includes('reboot')));
    assert.equal(p.ctx.pools, undefined);
    for (const name of ['dashboard', 'login', 'pools', 'qr_login', 'upgrade']) {
        const html = fs.readFileSync(path.join(__dirname, '../assets', name + '.html'), 'utf8');
        for (const m of html.matchAll(/<input\b[^>]*type="password"[^>]*>/gi))
            assert(!/\smax=/.test(m[0]), name);
    }
    const up = page('upgrade');
    up.ctx.FormData = class {
        append() {}
    };
    up.ctx.XMLHttpRequest = class {
        constructor() {
            this.upload = {};
        }
        open(m, u) {
            this.method = m;
            this.url = u;
        }
        send() {
            up.calls.push({method: this.method, url: this.url, xhr: this});
        }
    };
    assert.equal(up.ctx.WebSocket, undefined);
    up.ctx.fileChosen([{name: 'fw.aup'}]);
    const startUpload = () => {
        up.ctx.upload();
        return up.calls.at(-1).xhr;
    };
    const upState = () => up.ctx.document.querySelector('.upgrade-state').textContent;
    const reply = (st, code, msg) => '{"STATUS":[{"STATUS":"' + st + '","When":1,"Code":' + code +
        ',"Msg":"' + msg + '"}],"id":1}';
    let x = startUpload();
    assert.equal(up.calls.at(-1).method, 'POST');
    assert.equal(up.calls.at(-1).url, '/api/v1/upgrade');
    x.upload.onprogress({lengthComputable: true, loaded: 25, total: 100});
    assert.equal(upState(), 'Uploading 25%');
    assert.equal(up.ctx.document.querySelector('.progress-fill').style.width, '25%');
    x.upload.onload();
    assert.equal(upState(), 'Writing the image, do not power off the device...');
    const nAlerts = up.alerts.length;
    x.onerror();
    assert.equal(up.alerts.length, nAlerts + 1);
    assert.equal(up.alerts.at(-1), 'The upload did not reach the device.');
    assert.equal(upState(), 'Upgrade failed');
    x = startUpload();
    x.status = 500;
    x.responseText = reply('E', 500, 'Upgrade failed(-6), percent(100)');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Image sha256 mismatch (-6), percent 100%');
    assert.equal(upState(), 'Upgrade failed');
    x = startUpload();
    x.status = 200;
    x.responseText = reply('E', 500, 'Upgrade failed(-4), percent(3)');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Image signature verification failed (-4), percent 3%');
    assert.equal(upState(), 'Upgrade failed');
    x = startUpload();
    x.status = 500;
    x.responseText = reply('E', 500, 'Upgrade failed(-9), percent(5)');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade failed(-9), percent(5)');
    assert.equal(upState(), 'Upgrade failed');
    x = startUpload();
    x.status = 409;
    x.responseText = reply('E', 500, 'Another firmware upgrade is in progress');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Another firmware upgrade is in progress');
    x = startUpload();
    x.status = 500;
    x.responseText = '{"STATUS":[{"STATUS":"E","When":1,"Code":500}],"id":1}';
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade failed');
    x = startUpload();
    x.status = 502;
    x.responseText = '<html>Bad Gateway</html>';
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade failed');
    x = startUpload();
    x.status = 200;
    x.responseText = 'Upgrade ok(1), percent(100)';
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade failed');
    assert.notEqual(upState(), 'Upgrade successful');
    x = startUpload();
    x.status = 202;
    x.responseText = reply('S', 200, 'Upgrade status unknown, device reported no upgrade result');
    assert.notEqual(up.ctx.document.querySelector('.loading').style.display, 'flex');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade status unknown, device reported no upgrade result. ' +
        'Do not upload again: wait for the device to restart, then check the firmware version.');
    assert.equal(upState(), 'Upgrade status not reported');
    assert.equal(up.ctx.document.querySelector('.mask').style.display, 'block');
    assert.equal(up.ctx.document.querySelector('.loading').style.display, 'flex');
    up.ctx.document.querySelector('.mask').style.display = 'none';
    up.ctx.document.querySelector('.loading').style.display = 'none';
    x = startUpload();
    x.status = 202;
    x.responseText = '';
    x.onload();
    assert.equal(up.alerts.at(-1),
        'Do not upload again: wait for the device to restart, then check the firmware version.');
    assert.equal(up.ctx.document.querySelector('.loading').style.display, 'flex');
    up.ctx.document.querySelector('.mask').style.display = 'none';
    up.ctx.document.querySelector('.loading').style.display = 'none';
    x = startUpload();
    x.status = 200;
    x.responseText = reply('S', 200, 'Upgrade ok(1), percent(100)');
    x.onload();
    assert.equal(up.alerts.at(-1), 'Upgrade successful. The device is restarting.');
    assert.equal(upState(), 'Upgrade successful');
    assert.equal(up.ctx.document.querySelector('.loading').style.display, 'flex');
    assert(!fs.readFileSync(path.join(__dirname, '../assets/upgrade.html'), 'utf8')
        .includes('WebSocket'));
    assert(/id="password"[^>]*maxlength="64"/.test(
        fs.readFileSync(path.join(__dirname, '../assets/login.html'), 'utf8')));

    const glueSrc = fs.readFileSync(path.join(__dirname, '../web/civetweb_glue.c'), 'utf8');
    for (const logo of ['pelish-logo.png', 'canaan-logo.png']) {
        const buf = fs.readFileSync(path.join(__dirname, '../assets', logo));
        assert(buf.length > 0 && buf.subarray(0, 8).equals(
            Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a])));
        assert(glueSrc.includes('"/' + logo + '"'));
    }
    for (const name of ['dashboard', 'login', 'pools', 'qr_login', 'upgrade']) {
        const html = fs.readFileSync(path.join(__dirname, '../assets', name + '.html'), 'utf8');
        assert(html.includes('src="pelish-logo.png"'), name + ' missing pelish-logo.png');
        assert(html.includes('src="canaan-logo.png"'), name + ' missing canaan-logo.png');
        assert(html.includes('rel="icon" type="image/png" href="pelish-logo.png"'),
            name + ' missing favicon');
        assert(!html.includes('<img src="" alt="">'), name + ' has empty logo img');
    }
})().catch(e => {
    console.error(e);
    process.exit(1);
});
