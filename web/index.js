import "weui";
import "./styles.css";
import QRCode from "qrjs";
import "flexi-color-picker/themes.css";
import "flexi-color-picker";
import ReconnectingWebSocket from "reconnecting-websocket";
import CConsole from "./cconsole.js";
import { startRecord, stopRecord } from "./audiohelper.js";
import { rgb2hex } from "./utils.js";

const DEV_MODE = process.env.NODE_ENV !== "production";
// 需与 enum EffectType 保持一致
const LIGHT_MODES = {
    0: "constant",
    1: "blink",
    2: "breath",
    3: "chase",
    4: "rainbow",
    5: "stream",
    6: "animation",
    7: "music",
    8: "custom"
};

const ws = new ReconnectingWebSocket("ws://" + (DEV_MODE ? "rgblight" : window.location.hostname) + ":81/", ["arduino"], {
    maxEnqueuedMessages: 0,
    startClosed: true,
    debug: DEV_MODE
});
const uiStack = new Array();

// cmd console
const cconsole = new CConsole({
    executeCallback(command) {
        cconsole.print("发送: " + command);
        ws.send(command);
    }
});

document.getElementById("console-open").onclick = function() {
    document.getElementById("console").style.display = "block";
}

document.getElementById("console-close").onclick = function() {
    document.getElementById("console").style.display = "none";
}

// tab bar
for (let element of document.getElementsByClassName("weui-tabbar__item")) {
    element.onclick = function() {
        let title = this.getElementsByClassName("weui-tabbar__label")[0].innerHTML;
        document.getElementById("title").getElementsByClassName("titlebar__title")[0].innerHTML = title;
        let lastItem = document.getElementsByClassName("weui-bar__item_on")[0];
        lastItem.classList.remove("weui-bar__item_on");
        document.getElementById(lastItem.id.replace("tab", "panel")).style.display = "none";
        this.classList.add("weui-bar__item_on");
        document.getElementById(this.id.replace("tab", "panel")).style.display = "block";
        if (this.id == "tab2") {
            refreshFileList();
        }
    }
}

// light effect
const colorpicker = window.ColorPicker(
    document.getElementById("colorpicker"),
    function(hex, hsv, rgb) {
        colorpicker.rgb = rgb;
        if (colorpicker.prevent) return;
        document.getElementById("r").value = rgb.r;
        document.getElementById("g").value = rgb.g;
        document.getElementById("b").value = rgb.b;
        sendMode();
    });

document.getElementById("brightness").onchange = function() {
    cconsole.execute("brightness," + this.value);
}

document.getElementById("temperature").onchange = function() {
    cconsole.execute("temperature," + this.value);
}

document.getElementById("refreshRate").onchange = function() {
    cconsole.execute("fps," + this.value);
}

function updateMode(newModeButton) {
    let oldModeButton = document.getElementById("mode").getElementsByClassName("weui-btn_disabled")[0];
    oldModeButton.removeAttribute("disabled");
    oldModeButton.classList.remove("weui-btn_disabled");
    newModeButton.setAttribute("disabled", "");
    newModeButton.classList.add("weui-btn_disabled");
    for (let element of document.getElementsByClassName("mode-setting")) {
        let modes = element.getAttribute("mode").split("|");
        element.style.display = modes.includes(newModeButton.id) ? "block" : "none";
    }

    let mode = newModeButton.id;
    if (mode == "animation") {
        fetch("/list?path=/animations").then((response) => {
            if (!response.ok) return;
            response.json().then((files) => {
                let animName = document.getElementById("animName");
                animName.innerHTML = "<option value='' selected></option>";
                for (let file of files) {
                    if (file["isDir"]) continue;
                    let option = document.createElement("option");
                    option.value = file["name"];
                    option.innerText = file["name"].split(".")[0];
                    animName.appendChild(option);
                }
            });
        });
    } else if (mode == "music") {
        startRecord(function(result) {
            cconsole.execute(String(Number(result).toFixed(2)));
        });
    }
}

function sendMode() {
    let mode = document.getElementById("mode").getElementsByClassName("weui-btn_disabled")[0].id;
    let args = ["mode", mode];
    if (mode == "constant" || mode == "blink" || mode == "breath" || mode == "chase") {
        let rgb = colorpicker.rgb;
        let hex = rgb2hex(rgb.r, rgb.g, rgb.b);
        args.push(hex);
    }
    if (mode == "blink" || mode == "breath" || mode == "chase") {
        args.push(document.getElementById("lastTime").value);
    }
    if (mode == "blink" || mode == "breath") {
        args.push(document.getElementById("interval").value);
    }
    if (mode == "rainbow" || mode == "stream") {
        args.push(document.getElementById("delta").value);
    }
    if (mode == "animation") {
        args.push(document.getElementById("animName").value);
    }
    cconsole.execute(args.join(","));
}

for (let element of document.getElementById("mode").children) {
    element.onclick = function() {
        let oldMode = document.getElementById("mode").getElementsByClassName("weui-btn_disabled")[0].id;
        let newMode = this.id;
        if (oldMode == newMode) return;

        if (oldMode == "music") {
            stopRecord();
        }
        
        updateMode(this);
        if (newMode == "blink") {
            document.getElementById("interval").value = 1.0;
            document.getElementById("lastTime").value = 1.0;
        } else if (newMode == "breath") {
            document.getElementById("interval").value = 1.0;
            document.getElementById("lastTime").value = 0.5;
        } else if (newMode == "chase") {
            document.getElementById("lastTime").value = 0.2;
        } else if (newMode == "rainbow") {
            document.getElementById("delta").value = 1;
        } else if (newMode == "stream") {
            document.getElementById("delta").value = 1;
        }

        sendMode();
    }
}

document.getElementById("r").onchange =
    document.getElementById("g").onchange =
    document.getElementById("b").onchange =
    function() {
        let r = parseInt(document.getElementById("r").value);
        let g = parseInt(document.getElementById("g").value);
        let b = parseInt(document.getElementById("b").value);
        colorpicker.prevent = true;
        colorpicker.setRgb({ r: r, g: g, b: b });
        colorpicker.prevent = false;
        sendMode();
    }

document.getElementById("lastTime").onchange =
    document.getElementById("interval").onchange =
    document.getElementById("delta").onchange =
    document.getElementById("animName").onchange =
    function() {
        sendMode();
    }

// file manager
const viewPath = ["/"];

function refreshFileList() {
    fetch("/list?path=" + viewPath.join("")).then((response) => {
        if (!response.ok) return;
        response.json().then((files) => {
            let fileList = document.getElementById("files");
            fileList.innerHTML = "";
            if (viewPath.length > 1) {
                files.unshift({
                    "name": "..",
                    "isDir": true
                })
            }
            for (let file of files) {
                let item = document.createElement("div");
                item.classList.add("weui-cell");
                if (file["isDir"]) {
                    item.classList.add("weui-cell_access");
                    if (file["name"] == "..") {
                        item.onclick = function() {
                            viewPath.pop();
                            refreshFileList();
                        }
                    } else {
                        item.onclick = function() {
                            viewPath.push(file["name"] + "/");
                            refreshFileList();
                        }
                    }
                }
                let bd = document.createElement("span");
                bd.classList.add("weui-cell__bd");
                bd.innerText = file["name"];
                item.appendChild(bd);
                let ft = document.createElement("span");
                ft.classList.add("weui-cell__ft");
                if (!file["isDir"]) {
                    let dl = document.createElement("a");
                    dl.classList.add("weui-btn", "weui-btn_mini", "weui-btn_primary");
                    dl.innerText = "下载";
                    dl.href = "/download?path=" + viewPath.join("") + file["name"];
                    dl.download = file["name"];
                    ft.appendChild(dl);
                    let del = document.createElement("a");
                    del.classList.add("weui-btn", "weui-btn_mini", "weui-btn_warn");
                    del.innerText = "删除";
                    del.onclick = function() {
                        fetch("/delete?path=" + viewPath.join("") + file["name"]).then((response) => {
                            if (!response.ok) return;
                            refreshFileList();
                        });
                    }
                    ft.appendChild(del);
                }
                item.appendChild(ft);
                fileList.appendChild(item);
            }
        });
    });
}

function uploadFile(path, file) {
    let formData = new FormData();
    formData.append("file", file);
    return fetch("/upload?path=" + path, {
        method: "POST",
        body: formData
    });
}

document.getElementById("upload").onclick = function() {
    let input = document.createElement("input");
    input.type = "file";
    input.onchange = function() {
        uploadFile(viewPath.join(""), input.files[0]).then((response) => {
            if (!response.ok) return;
            refreshFileList();
        }).finally(() => {
            input.remove();
        });
    }
    input.click();
}

document.getElementById("refresh").onclick = function() {
    refreshFileList();
}

// settings


// load
window.onload = function() {
    const setQrcode = (data) => document.getElementById("qrcode").src = QRCode.generatePNG(data, {
        modulesize: 10,
        margin: 2.5
    });

    ws.addEventListener("open", () => {
        let date = new Date().toLocaleString();
        cconsole.print("已于 " + date + " 成功与 " + ws.url + " 建立连接!");
    });
    ws.addEventListener("error", (error) => {
        cconsole.print("错误: " + error.message);
    });
    ws.addEventListener("message", (msg) => {
        cconsole.print("接收: " + msg.data);
    });
    ws.reconnect();

    fetch("/version").then((response) => {
        if (!response.ok) return;
        response.json().then((version) => {
            document.getElementById("model").innerText = version["model"];
            document.getElementById("version").innerText = version["version"];
            document.getElementById("sdkversion").innerText = version["sdkVersion"];
        });
    });
    fetch("/config").then((response) => {
        if (!response.ok) return;
        response.json().then(config => {
            document.title = config["name"];
            setQrcode("http://" + config["ip"] + "/");
            document.getElementById("ssid").innerHTML = config["ssid"] || "未连接";
            document.getElementById("name").innerText = config["name"];
            document.getElementById("hostname").innerText = config["hostname"];

            document.getElementById("brightness").value = config["brightness"];
            document.getElementById("temperature").value = config["temperature"];
            document.getElementById("refreshRate").value = config["refreshRate"];

            updateMode(document.getElementById(LIGHT_MODES[config["mode"]]));
            let color = config["color"] || 0xFFFFFF;
            let rgb = {
                r: (color & 0xFF0000) >> 16,
                g: (color & 0x00FF00) >> 8,
                b: (color & 0x0000FF) >> 0
            };
            colorpicker.prevent = true;
            colorpicker.setRgb(rgb);
            document.getElementById("r").value = rgb["r"];
            document.getElementById("g").value = rgb["g"];
            document.getElementById("b").value = rgb["b"];
            colorpicker.prevent = false;
            document.getElementById("lastTime").value = config["lastTime"] || 1.0;
            document.getElementById("interval").value = config["interval"] || 1.0;
            document.getElementById("delta").value = config["delta"] || 1;
            document.getElementById("animName").value = config["animName"] || "";
        });
    });

    setQrcode(window.location.href);
}
