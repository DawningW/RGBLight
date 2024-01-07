/*
 * 工具函数
 */

/**
 * polyfill for String.prototype.padStart
 * 
 * @param {String} str the string to pad
 * @param {Number} width pad the string at start with '0' to the given width
 * @returns {String} the result string
 */
function zfill(str, width) {
    str = String(str);
    while (str.length < width) {
        str = "0" + str;
    }
    return str;
}

/**
 * convert rgb color to hex color
 * 
 * @param {Number} r r component of color
 * @param {Number} g g component of color
 * @param {Number} b b component of color
 * @returns {String} hex color string
 */
function rgb2hex(r, g, b) {
    return "#" + zfill(r.toString(16), 2) + zfill(g.toString(16), 2) + zfill(b.toString(16), 2);
}

/**
 * polyfill for TextDecoder
 * 
 * @param {Uint8Array} bytes utf-8 encoded bytes
 * @returns {String} decoded string
 */
function bytes2str(bytes) {
    return String.fromCodePoint(...Array.from(bytes));
}

export { zfill, rgb2hex, bytes2str };
