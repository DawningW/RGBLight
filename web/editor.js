import { getProject, types, val } from "@theatre/core";
import studio from "@theatre/studio";
import { getPointerParts } from '@theatre/dataverse';

const studioPrivate = window.__TheatreJS_StudioBundle._studio;
let projectId = "";
let project = null;
let objs = [];

let leds = [];
let ledCounts = 0;
if (LIGHT_TYPE.type == "LightStrip") {
    leds = [parseInt(LIGHT_TYPE.args[0])];
    ledCounts = leds[0];
} else if (LIGHT_TYPE.type == "LightPanel") {
    leds = [parseInt(LIGHT_TYPE.args[0]), parseInt(LIGHT_TYPE.args[1])];
    ledCounts = leds[0] * leds[1];
} else if (LIGHT_TYPE.type == "LightDisc") {
    leds = LIGHT_TYPE.args.slice(1).map((s) => parseInt(s));
    ledCounts = leds.reduce((a, b) => a + b, 0);
}
const renderLeds = () => {
    if (LIGHT_TYPE.type == "LightStrip") {
        return `
            <div id="leds" style="display: flex; flex-direction: row; gap: 5px;">
                ${Array(ledCounts).fill().map((_, i) => `<div style="width: 20px; height: 20px;"></div>`).join("")}
            </div>
        `;
    } else if (LIGHT_TYPE.type == "LightPanel") {
        return `
            <div id="leds" style="display: grid; grid-template-columns: repeat(${leds[0]}, 1fr); grid-template-rows: repeat(${leds[1]}, 1fr); gap: 1px;">
                ${Array(ledCounts).fill().map((_, i) => {
                    let x = i % leds[0];
                    let y = Math.floor(i / leds[0]);
                    if (y % 2 == 1) {
                        x = leds[0] - x - 1;
                    }
                    y = leds[1] - y - 1;
                    return `<div style="width: 20px; height: 20px; order: ${y * leds[0] + x}"></div>`;
                }).join("")}
            </div>
        `;
    } else if (LIGHT_TYPE.type == "LightDisc") {
        const INITIAL_ANGLES = [Math.PI / 12, 0, -Math.PI / 4]; /* 15°, 0°, -45° */
        return `
            <div id="leds" style="position: relative;">
                ${leds.map((count, i) =>
                    Array(count).fill().map((_, j) => {
                        let r = (2 * (leds.length - i - 1) + 1) * 40;
                        let angle = (2 * Math.PI / count) * j + INITIAL_ANGLES[i] + Math.PI / 2;
                        let x = r * Math.cos(angle);
                        let y = r * Math.sin(angle);
                        return `<div style="position: absolute; width: 20px; height: 20px; left: ${x}px; bottom: ${y}px; transform: rotate(${Math.PI / 2 - angle}rad);"></div>`;
                    }).join("")
                ).join("")}
            </div>
        `;
    }
    return "";
};

studio.extend({
    id: 'rgblight-extension',
    toolbars: {
        global(set, studio) {
            set([
                {
                    type: 'Icon',
                    title: 'Save',
                    svgSource: '💾',
                    onClick: () => saveAnim(project, projectId)
                },
                {
                    type: 'Icon',
                    title: 'Import Pixel Art',
                    svgSource: '🖼️',
                    onClick: () => importPixelArt()
                },
                {
                    type: 'Icon',
                    title: 'Close',
                    svgSource: '❌',
                    onClick: () => studio.ui.hide()
                }
            ])
        },
    },
    panes: [
        {
            class: 'Preview',
            mount({paneId, node}) {
                node.innerHTML = `<div style="width: 100%; height: 100%; display: flex; justify-content: center; align-items: center;">${renderLeds()}</div>`;
                return () => {}
            },
        },
    ],
});
studio.initialize({
    usePersistentStorage: false,
});
studio.createPane("Preview");
studio.ui.hide();

function uploadAnim(name, data) {
    let file = new File([data], name);
    let formData = new FormData();
    formData.append("file", file);
    return fetch("/upload?path=/animations/", {
        method: "POST",
        body: formData
    });
}

async function loadAnim(name) {
    try {
        // 加载动画json
        let response = await fetch(`/download?path=/animations/${name}.json`);
        if (response.ok) {
            return await response.json();
        }
    } catch (_) {}
}

async function saveAnim(project, name) {
    $toast("loading", "保存动画中", -1);

    try {
        // 保存动画json
        const state = studio.createContentOfSaveFile(name);
        const jsonStr = JSON.stringify(state);
        // console.debug(jsonStr);
        let response = await uploadAnim(`${name}.json`, jsonStr);
        if (!response.ok) throw new Error();
        
        // 保存板端使用的逐帧动画
        const data = [];
        const sequence = project.sheet("Light Animation").sequence;
        const pos = sequence.position;
        sequence.position = 0;
        while (sequence.position < val(sequence.pointer.length)) {
            for (const obj of objs) {
                const { r, g, b, a } = val(obj.props.color);
                data.push(parseInt(r * 255), parseInt(g * 255), parseInt(b * 255));
            }
            sequence.position += 1 / 30;
        }
        sequence.position = pos;
        const buffer = new ArrayBuffer(data.length);
        const view = new Uint8Array(buffer);
        for (let i = 0; i < data.length; i++) {
            view[i] = data[i];
        }
        // console.debug(buffer);
        response = await uploadAnim(`.${name}.bin`, buffer);
        if (!response.ok) throw new Error();

        $toast("success", "保存动画成功");
    } catch (_) {
        $toast("fail", "保存动画失败");
    }
}

async function importPixelArt() {
    if (LIGHT_TYPE.type != "LightPanel") {
        $toast("fail", "当前仅矩形灯板支持导入像素画");
        return;
    }

    const input = document.createElement("input");
    input.type = "file";
    input.accept = "image/*";
    input.onchange = async () => {
        const file = input.files[0];
        if (!file) return;

        const image = new Image();
        image.src = URL.createObjectURL(file);
        image.onload = () => {
            const canvas = document.createElement("canvas");
            canvas.width = leds[0];
            canvas.height = leds[1];
            const ctx = canvas.getContext("2d");
            ctx.drawImage(image, 0, 0, leds[0], leds[1]);
            const data = ctx.getImageData(0, 0, leds[0], leds[1]).data;

            const sequence = project.sheet("Light Animation").sequence;
            studio.transaction(({ set, unset }) => {
                for (let i = 0; i < ledCounts; i++) {
                    let x = i % leds[0];
                    let y = Math.floor(i / leds[0]);
                    if (y % 2 == 1) {
                        x = leds[0] - x - 1;
                    }
                    y = leds[1] - y - 1;
                    let di = (x + y * leds[0]) * 4;
                    set(objs[i].props.color, { r: data[di] / 255, g: data[di + 1] / 255, b: data[di + 2] / 255, a: 1 });
                }
            });
        };
    };
    input.click();
}

export async function editAnimation(name) {
    const hideToast = $toast("loading", "读取动画中", -1);

    const atom = studioPrivate._store._atom;
    atom.setByPointer(atom.pointer.ahistoric.coreByProject, {});
    atom.setByPointer(atom.pointer.historic.coreByProject, {});
    atom.setByPointer(atom.pointer.ephemeral.coreByProject, {});
    const pointer = studioPrivate._projectsProxy._currentPointerBox.prism.getValue();
    const meta = getPointerParts(pointer);
    meta.root.setByPointer(meta.root.pointer.projects, {});

    const state = await loadAnim(name);
    studio.ui.restore();
    project = getProject(name, { state });
    projectId = project.address.projectId;
    const sheet = project.sheet("Light Animation");

    objs = [];
    const ledElements = document.getElementById("theatrejs-studio-root").shadowRoot.getElementById("leds").children;
    for (let i = 0; i < ledCounts; i++) {
        const objName = `LED ${i + 1}`;
        let obj = sheet.__experimental_getExistingObject(objName);
        if (!obj) {
            obj = sheet.object(`LED ${i + 1}`, {
                color: types.rgba({ r: 0, g: 0, b: 0, a: 1 }),
            });
        }

        obj.onValuesChange((obj) => {
            ledElements[i].style.backgroundColor = `rgba(${obj.color.r * 255}, ${obj.color.g * 255}, ${obj.color.b * 255}, ${obj.color.a})`;
        });

        objs.push(obj);
    }
    
    await project.ready;

    const sequence = sheet.sequence;
    if (!state) {
        studio.transaction(({ set, unset }) => {
            set(sequence.pointer.length, 5);
        }, false);

        for (const obj of objs) {
            studioPrivate.transaction(({ stateEditors }) => {
                const {path, root} = getPointerParts(obj.props.color);
                const propAddress = {...root.address, pathToProp: path};
                stateEditors.coreByProject.historic.sheetsById.sequence.setPrimitivePropAsSequenced(
                    propAddress,
                );
            }, false);
        }
    }

    hideToast();
}
