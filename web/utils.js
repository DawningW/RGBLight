/*
 * 工具函数
 */

function zfill(str, width) {
    str = String(str);
    while (str.length < width) {
        str = "0" + str;
    }
    return str;
}

function rgb2hex(r, g, b) {
    return "#" + zfill(r.toString(16), 2) + zfill(g.toString(16), 2) + zfill(b.toString(16), 2);
}

export { zfill, rgb2hex };
