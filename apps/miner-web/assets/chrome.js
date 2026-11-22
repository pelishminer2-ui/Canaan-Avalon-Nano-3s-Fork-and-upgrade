/* Theme choice and shell helpers. Pages call these after chrome.js loads. */
function applyTheme(theme) {
    if (theme !== "dark")
        theme = "light";
    var root = document.documentElement;
    if (root && root.setAttribute)
        root.setAttribute("data-theme", theme);
    try {
        localStorage.setItem("pelish-theme", theme);
    } catch (e) {}
    var secure = "";
    try {
        if (location.protocol === "https:")
            secure = "; Secure";
    } catch (e2) {}
    document.cookie = "pelish-theme=" + theme + "; path=/; max-age=31536000; SameSite=Strict" + secure;
    markThemeButtons(theme);
}

function markThemeButtons(theme) {
    var light = document.getElementById("theme-light");
    var dark = document.getElementById("theme-dark");
    if (light)
        light.setAttribute("aria-pressed", theme === "light" ? "true" : "false");
    if (dark)
        dark.setAttribute("aria-pressed", theme === "dark" ? "true" : "false");
}

function stampLoadTime() {
    var el = document.querySelector(".header-load-time");
    if (!el)
        return;
    var now = new Date();
    var p = function (n) { return (n < 10 ? "0" : "") + n; };
    el.textContent = now.getFullYear() + "-" + p(now.getMonth() + 1) + "-" + p(now.getDate()) +
        " " + p(now.getHours()) + ":" + p(now.getMinutes()) + ":" + p(now.getSeconds());
}

function shellHeader(req) {
    if (!req || !req.hwtype)
        return;
    var name = document.querySelector(".header-name");
    if (name) {
        var device = req.hwtype;
        if (device.indexOf("Nano3s") > 0)
            name.innerHTML = "Avalon Nano 3S";
        else if (device.indexOf("Mini3") > 0)
            name.innerHTML = "Avalon Mini 3";
        else if (device.indexOf("Q") > 0)
            name.innerHTML = "Avalon Q";
    }
    var status = req.sys_status;
    var dot = document.querySelector(".header-status");
    var text = document.querySelector(".header-status-text");
    var pill = document.querySelector(".status-pill");
    if (status == "0") {
        if (dot) dot.style.backgroundColor = "#a1aabd";
        if (text) text.textContent = "Init";
    } else if (status == "1") {
        if (dot) dot.style.backgroundColor = "#26e68e";
        if (text) text.textContent = "Normal";
    } else if (status == "2") {
        if (dot) dot.style.backgroundColor = "#a1aabd";
        if (text) text.textContent = "Idle";
    }
    if (pill && pill.classList)
        pill.classList.toggle("is-ok", status == "1");
    var mac = document.querySelector(".macaddress");
    var ver = document.querySelector(".version");
    if (mac && req.mac)
        mac.textContent = req.mac;
    if (ver && req.version)
        ver.textContent = req.version;
}

function loadSsid() {
    fetch("/api/v1/litestats", {cache: "no-store"}).then(function (r) {
        if (!r.ok)
            throw new Error("litestats");
        return r.json();
    }).then(function (j) {
        var sum = j && j.STATS && j.STATS[0] && j.STATS[0].MMID0Summary;
        var ssid = sum && sum.SSID;
        var el = document.querySelector(".ssid");
        if (el && typeof ssid === "string" && ssid.length)
            el.textContent = ssid;
    }).catch(function () {});
}

function shellHeaderFetch() {
    fetch("/api/v1/dashboard?num=" + Math.random(), {cache: "no-store"}).then(function (r) {
        if (!r.ok)
            throw new Error("dashboard");
        return r.json();
    }).then(shellHeader).catch(function () {});
    loadSsid();
}

function logout() {
    if (!confirm("Are you sure you want to sign out?"))
        return;
    document.cookie = "auth=; expires=Thu, 01 Jan 1970 00:00:00 UTC; path=/;";
    var done = function () { location.href = "login.html"; };
    if (typeof fetch === "function")
        fetch("/api/v1/session", {method: "DELETE"}).finally(done);
    else
        done();
}

function navReboot() {
    if (!confirm("Are you sure you want to restart?"))
        return;
    fetch("/api/v1/system/reboot", {method: "POST"}).then(function (data) {
        if (!data.ok)
            throw new Error("The device rejected the reboot request");
        var mask = document.querySelector(".mask");
        var loading = document.querySelector(".loading");
        if (mask)
            mask.style.display = "block";
        if (loading)
            loading.style.display = "flex";
        setTimeout(function () { location.reload(); }, 30000);
    }).catch(function (error) {
        alert(error.message || "Reboot failed");
    });
}

document.addEventListener("DOMContentLoaded", function () {
    var theme = "light";
    try {
        var attr = document.documentElement && document.documentElement.getAttribute("data-theme");
        if (attr === "dark" || attr === "light")
            theme = attr;
    } catch (e) {}
    markThemeButtons(theme);
});
