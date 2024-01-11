/*
 * 音乐律动
 */

import {SoundProcessor} from "sound-processor";

const AudioContext = window.AudioContext || window.webkitAudioContext || window.mozAudioContext;
let ctx;
let analyser;
let frequencies;
let soundProcessor;
let mediaStream;
let audioSource;
let timerId;

// cubic-bezier(0.65, 0, 0.35, 1)
function easeInOutCubic(x) {
    return x < 0.5 ? 4 * x * x * x : 1 - Math.pow(-2 * x + 2, 3) / 2;
}

function startRecord(callback) {
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
			endFrequency: 4500,
			outBandsQty: 1,
			tWeight: true,
			aWeight: true
		});
    }

    if (!window.navigator.mediaDevices) {
        alert("因浏览器策略限制无法启动音频采集, 请前往 chrome://flags/#unsafely-treat-insecure-origin-as-secure, 将此页面的地址添加到列表中并重启浏览器");
        return;
    }

    window.navigator.mediaDevices.getDisplayMedia({
        "video": true,
        "audio": true
    }).then((stream) => {
        mediaStream = stream;
        audioSource = ctx.createMediaStreamSource(stream);
        audioSource.connect(analyser);
        timerId = setInterval(() => {
            analyser.getByteFrequencyData(frequencies);
            let result = soundProcessor.process(frequencies);
            let scale = easeInOutCubic(result[0] / 255);
            callback(scale);
        }, 50);
    }).catch((err) => {
        alert("请共享整个屏幕并勾选同时共享系统音频以便使用音乐律动模式: " + err.message);
    });
}

function stopRecord() {
    if (mediaStream && audioSource) {
        clearInterval(timerId);
        audioSource.disconnect();
        mediaStream.getTracks().forEach(track => track.stop());
        mediaStream = audioSource = null;
    }
}

export {
    startRecord,
    stopRecord
};
