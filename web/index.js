import "weui";
import "./styles.css";
import QRCode from "qrjs";
import "flexi-color-picker/themes.css";
import "flexi-color-picker";
import ReconnectingWebSocket from "reconnecting-websocket";
import CConsole from "./cconsole.js";
import { startRecord, stopRecord } from "./audiohelper.js";
import { rgb2hex, bytes2str } from "./utils.js";

/**
 * @type {typeof import("./editor.js")}
 */
let editor = null;
async function loadEditor() {
    if (editor == null) {
        const hideToast = $toast("loading", "正在加载动画编辑器", -1);
        try {
            editor = await import(/* webpackChunkName: "editor" */ "./editor.js");
            hideToast();
        } catch (err) {
            $toast("fail", "加载失败");
        }
    }
    return editor;
}

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

const ws = new ReconnectingWebSocket("ws://" + (DEV_MODE ? "rgblight.local" : window.location.hostname) + ":81/", ["arduino"], {
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

// popups (dialog, toast, info bar)
window.$dialog = function(title, content, onPrimaryClick, onDefaultClick) {
    let dialog = document.getElementById("dialog");
    dialog.getElementsByClassName("weui-dialog__title")[0].innerText = title;
    dialog.getElementsByClassName("weui-dialog__bd")[0].innerHTML = content;
    let defaultBtn = dialog.getElementsByClassName("weui-dialog__btn_default")[0];
    let primaryBtn = dialog.getElementsByClassName("weui-dialog__btn_primary")[0];
    if (onPrimaryClick) {
        defaultBtn.innerText = "取消";
        primaryBtn.style.display = "block";
        primaryBtn.onclick = function() {
            dialog.style.display = "none";
            onPrimaryClick();
        }
    } else {
        defaultBtn.innerText = "关闭";
        primaryBtn.style.display = "none";
        primaryBtn.onclick = null;
    }
    defaultBtn.onclick = function() {
        dialog.style.display = "none";
        if (onDefaultClick) onDefaultClick();
    }
    dialog.style.display = "block";
}

window.$toast = function(status, message, duration) {
    let toast = document.getElementById("toast");
    let icon = toast.getElementsByTagName("i")[0];
    icon.classList.value = "weui-icon_toast";
    icon.innerHTML = "";
    if (status == "success") {
        icon.classList.add("weui-icon-success-no-circle");
    } else if (status == "fail") {
        icon.classList.add("weui-icon-warn");
    } else if (status == "loading") {
        icon.classList.add("weui-primary-loading");
        icon.innerHTML = '<span class="weui-primary-loading__dot"></span>';
    }
    toast.getElementsByClassName("weui-toast__content")[0].innerText = message;
    toast.style.display = "block";
    if (duration != -1) {
        setTimeout(function() {
            toast.style.display = "none";
        }, duration || 2000);
    }
    return () => toast.style.display = "none";
}

window.$info = function(level, message, closeable) {
    let info = document.getElementById("infobar");
    info.classList.value = "weui-information-bar";
    if (level == "error") {
        info.classList.add("weui-information-bar_warn-strong");
    } else if (level == "warning") {
        info.classList.add("weui-information-bar_tips-strong");
    } else if (level == "info") {
        info.classList.add("weui-information-bar_tips-weak");
    }
    info.getElementsByClassName("weui-information-bar__bd")[0].innerText = message;
    let close = info.getElementsByClassName("weui-btn_icon")[0];
    close.style.display = closeable ? "block" : "none";
    close.onclick = closeable ? function() {
        info.style.display = "none";
    } : null;
    info.style.display = "flex";
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
            refreshFileList(true);
        } else {
            document.getElementById("add-anim").style.visibility = "hidden";
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
                    if (file["isDir"] || !file["name"].endsWith(".bin")) {
                        continue;
                    }
                    let option = document.createElement("option");
                    option.value = file["name"];
                    option.innerText = file["name"].split(".")[1];
                    animName.appendChild(option);
                }
            });
        });
    } else if (mode == "music") {
        startRecord(function(result) {
            cconsole.execute(String(Number(result).toFixed(2)));
        }, function(message) {
            stopRecord();
            $dialog("音乐律动初始化失败", message, null, updateMode.bind(null, oldModeButton));
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

function refreshFileList(refreshSpace = false) {
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
                if (file["name"] != ".." && file["name"].startsWith(".")) {
                    continue;
                }
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
                    if (viewPath.includes("animations/") && file["name"].endsWith(".json")) {
                        let edit = document.createElement("a");
                        edit.classList.add("weui-btn", "weui-btn_mini", "weui-btn_default");
                        edit.innerText = "编辑";
                        edit.onclick = function() {
                            loadEditor().then(({ editAnimation }) => {
                                editAnimation(file["name"].replace(".json", ""));
                            });
                        }
                        ft.appendChild(edit);
                    }
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
                            refreshFileList(true);
                        });
                    }
                    ft.appendChild(del);
                }
                item.appendChild(ft);
                fileList.appendChild(item);
            }
        });
    });
    if (refreshSpace) {
        fetch("/status").then((response) => {
            if (!response.ok) return;
            response.json().then((status) => {
                let total = Math.floor(status["fsTotalSpace"] / 1024);
                let used = Math.floor(status["fsUsedSpace"] / 1024);
                document.getElementById("space").innerText = `${used}K/${total}K`;
            });
        });
    }
    document.getElementById("add-anim").style.visibility =
        viewPath.join("") == "/animations/" ? "visible" : "hidden";
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
            refreshFileList(true);
        }).finally(() => {
            input.remove();
        });
    }
    input.click();
}

document.getElementById("refresh").onclick = function() {
    refreshFileList();
}

document.getElementById("add-anim").onclick = function() {
    $dialog("新建动画", `
        <input class="weui-input" type="text" placeholder="动画名称">
    `, function() {
        let input = document.getElementById("dialog").getElementsByClassName("weui-input")[0];
        if (input.value == "") {
            $toast("fail", "动画名称无效");
            return;
        }
        loadEditor().then(({ editAnimation }) => {
            editAnimation(input.value);
        });
    });
}

// settings
document.getElementById("connect").onclick = function() {
    document.getElementById("wifi").style.display = "block";
    document.getElementById("wifi-list").innerHTML = "正在搜索中...";
    let listener = (msg) => {
        document.getElementById("wifi-list").innerHTML = ""
        let data = JSON.parse(msg.data);
        for (let wifi of data) {
            let item = document.createElement("a");
            item.classList.add("weui-cell", "weui-cell_access");
            let bd = document.createElement("span");
            bd.classList.add("weui-cell__bd");
            bd.innerText = wifi["ssid"];
            item.appendChild(bd);
            let ft = document.createElement("span");
            ft.classList.add("weui-cell__ft");
            ft.innerText = wifi["rssi"];
            item.appendChild(ft);
            item.onclick = function() {
                $dialog("连接至 " + wifi["ssid"], `
                    <input class="weui-input" type="password" placeholder="密码">
                `, function() {
                    let input = document.getElementById("dialog").getElementsByClassName("weui-input")[0];
                    cconsole.execute("connect," + wifi["ssid"] + "," + input.value);
                    $toast("success", "已连接");
                    refreshConfig();
                });
            }
            document.getElementById("wifi-list").appendChild(item);
        }
        ws.removeEventListener("message", listener);
    };
    ws.addEventListener("message", listener);
    cconsole.execute("scan");
}

document.getElementById("wifi-close").onclick = function() {
    document.getElementById("wifi").style.display = "none";
}

document.getElementById("disconnect").onclick = function() {
    $dialog("断开连接", "确定要断开与此 WiFi 的连接吗?", function() {
        cconsole.execute("disconnect");
        $toast("fail", "已断开连接");
        refreshConfig();
    });
}

document.getElementById("change-name").onclick = function() {
    let name = document.getElementById("name").innerText;
    $dialog("修改设备名称", `
        <input class="weui-input" type="text" placeholder="设备名称" value="${name}">
    `, function() {
        let input = document.getElementById("dialog").getElementsByClassName("weui-input")[0];
        if (input.value == "") {
            $toast("fail", "设备名称无效");
            return;
        }
        cconsole.execute("name," + input.value);
        $toast("success", "已修改");
        refreshConfig();
    });
}

document.getElementById("change-hostname").onclick = function() {
    let hostname = document.getElementById("hostname").innerText;
    $dialog("修改访问地址", `
        <input class="weui-input" type="text" placeholder="主机名" value="${hostname}">
        <p>可通过 <span id="url"></span> 访问此页面</p> 
    `, function() {
        let input = document.getElementById("dialog").getElementsByClassName("weui-input")[0];
        if (input.value == "") {
            $toast("fail", "主机名无效");
            return;
        }
        cconsole.execute("name," + document.getElementById("name").innerText + "," + input.value);
        $toast("success", "已修改");
        refreshConfig();
    });
    let setUrl = (url) => document.getElementById("url").innerText = `http://${url}/ 或 http://${url}.local/`;
    document.getElementById("dialog").getElementsByClassName("weui-input")[0].oninput = function() {
        setUrl(this.value);
    }
    setUrl(hostname);
}

document.getElementById("upgrade").onclick = function() {
    let input = document.createElement("input");
    input.type = "file";
    input.onchange = async function() {
        $toast("loading", "正在升级", -1);
        try {
            let upgrade_pack = input.files[0];
            let buffer = await upgrade_pack.arrayBuffer();
            let data = new Uint8Array(buffer);
            let view = new DataView(buffer);
            let offset = 0;
            // header
            let magic = bytes2str(data.subarray(0, 4));
            if (magic != "UPKG") {
                throw new Error("无效的升级包");
            }
            let version = view.getUint32(4, true);
            if (version <= window.version["versionCode"]) {
                throw new Error("该升级包比当前版本旧, 无法安装");
            }
            let file_count = view.getUint32(8, true);
            let crc32 = view.getUint32(12, true);
            offset += 16;
            // item
            for (let i = 0; i < file_count; i++) {
                let magic = view.getUint8(offset++);
                if (magic != 0xAA) {
                    throw new Error("无效的资源文件");
                }
                let operation = view.getUint8(offset++);
                let end_index = data.indexOf(0x00, offset);
                let file_pathname = bytes2str(data.subarray(offset, end_index));
                offset = end_index + 1;
                let file_size = view.getUint32(offset, true);
                offset += 4;
                let file_content = data.subarray(offset, offset + file_size);
                offset += file_size;
                // upload res
                let file_path = file_pathname.substr(0, file_pathname.lastIndexOf("/"));
                let file_name = file_pathname.substr(file_pathname.lastIndexOf("/") + 1);
                let file = new File([file_content], file_name);
                let res = await uploadFile(file_path, file);
                if (await res.text() != "OK") {
                    throw new Error("资源文件上传失败");
                }
            }
            // upgrade
            let res = await fetch("/upgrade?path=/RGBLight.bin");
            if (await res.text() != "OK") {
                throw new Error("升级失败");
            }
        } catch (err) {
            $toast("fail", err.message);
            return;
        }
        // reload
        $toast("success", "升级成功");
        setTimeout(function() {
            window.location.reload();
        }, 2000);
    }
    input.click();
}

// load
const setQrcode = (data) => document.getElementById("qrcode").src = QRCode.generatePNG(data, {
    modulesize: 10,
    margin: 2.5
});

function refreshConfig() {
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
}

window.onload = function() {
    ws.addEventListener("open", () => {
        let date = new Date().toLocaleString();
        cconsole.print("已于 " + date + " 成功与 " + ws.url + " 建立连接!");
        document.getElementById("infobar").style.display = "none";
    });
    ws.addEventListener("error", (error) => {
        cconsole.print("错误: " + error.message);
        $info("error", "无法连接至小彩灯, 请检查设备状态!", true);
    });
    ws.addEventListener("message", (msg) => {
        cconsole.print("接收: " + msg.data);
    });
    ws.reconnect();

    fetch("/version").then((response) => {
        if (!response.ok) return;
        response.json().then((version) => {
            window.version = version;
            document.getElementById("model").innerText = version["model"];
            document.getElementById("version").innerText = version["version"];
            document.getElementById("sdkversion").innerText = version["sdkVersion"];
        });
    });
    refreshConfig();

    setQrcode(window.location.href);
}
