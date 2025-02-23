/*
 * 音乐律动
 */

import { SoundProcessor } from "sound-processor";

const AudioContext = window.AudioContext || window.webkitAudioContext || window.mozAudioContext;
let ctx;
let analyser;
let frequencies;
let soundProcessor;
let mediaStream;
let audioSource;
let timerId;

let outBands = 1;
if (LIGHT_TYPE.type == "LightPanel") {
    outBands = parseInt(LIGHT_TYPE.args[0]); // width of panel
}

// cubic-bezier(0.65, 0, 0.35, 1)
function easeInOutCubic(x) {
    return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
}

function startRecord(onData, onError) {
    if (!window.navigator.mediaDevices) {
        let url = "chrome://flags/#unsafely-treat-insecure-origin-as-secure";
        onError(`因浏览器策略限制无法启动音频采集, 请前往 ${url} (已自动复制到剪贴板), 将此页面的链接添加到列表中并重启浏览器`);
        navigator.clipboard.writeText(url);
        return;
    }

    window.navigator.mediaDevices.getDisplayMedia({
        "video": true,
        "audio": true
    }).then((stream) => {
        // 自 Chrome 71 起收到用户手势后才可使用 Web Audio API, 详见: https://developer.chrome.com/blog/autoplay?hl=zh-cn#web_audio
        if (!ctx) {
            ctx = new AudioContext();
            analyser = ctx.createAnalyser();
            analyser.fftSize = 1024;
            frequencies = new Uint8Array(analyser.frequencyBinCount);
            soundProcessor = new SoundProcessor({
                filterParams: {
                    sigma: 1,
                    radius: 2
                },
                sampleRate: ctx.sampleRate,
                fftSize: analyser.fftSize,
                startFrequency: 150,
                endFrequency: 6000,
                outBandsQty: outBands,
                tWeight: true,
                aWeight: true
            });
        }

        mediaStream = stream;
        audioSource = ctx.createMediaStreamSource(stream);
        audioSource.connect(analyser);
        timerId = setInterval(() => {
            analyser.getByteFrequencyData(frequencies);
            let result = soundProcessor.process(frequencies);
            let scaledResult = result.map((value) => easeInOutCubic(value / 255));
            onData(scaledResult);
        }, 50);
    }).catch((err) => {
        onError("请共享整个屏幕并勾选同时共享系统音频以便使用音乐律动模式, 错误详情: " + err.message);
    });
}

function stopRecord() {
    if (timerId) {
        clearInterval(timerId);
    }
    if (audioSource) {
        audioSource.disconnect();
    }
    if (mediaStream) {
        mediaStream.getTracks().forEach(track => track.stop());
    }
    mediaStream = audioSource = timerId = null;
}

export {
    startRecord,
    stopRecord
};
