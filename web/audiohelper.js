/*
 * 音乐律动
 */

const AudioContext = window.AudioContext || window.webkitAudioContext;
let ctx;
let analyser;
let frequencies;
let mediaStream;
let audioSource;
let timerId;

function startRecord(callback) {
    if (!ctx) {
        ctx = new AudioContext();
        analyser = ctx.createAnalyser();
        analyser.fftSize = 32; // 512
        analyser.smoothingTimeConstant = 0.85;
        frequencies = new Uint8Array(analyser.frequencyBinCount);
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
            // TODO 重写音乐律动算法
            let i = 0;
            while (i < frequencies.length && frequencies[i] != 0) i++;
            // let result = (i * 255 + frequencyArray[i]) / frequencyArray.length / 255;
            let result = i > 2 ? frequencies[i - 3] * 1.5 / 255 : 0.0;
            if (result > 1.0) result = 1.0;
            callback(result);
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
