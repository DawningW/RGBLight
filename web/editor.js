import { getProject, types, val } from "@theatre/core";
import studio from "@theatre/studio";
import { getPointerParts } from '@theatre/dataverse';

const studioPrivate = window.__TheatreJS_StudioBundle._studio;
let projectId = "";
let objs = [];

const LIGHT_TYPE = DEVICE_CONFIG["LIGHT_TYPE"];
let direction = "";
let leds = [];
if (LIGHT_TYPE.startsWith("LightStrip")) {
    const groups = LIGHT_TYPE.match(/LightStrip\s*<(.+),(.+)>/);
    leds = [parseInt(groups[1].trim())];
} else if (LIGHT_TYPE.startsWith("LightDisc")) {
    const groups = LIGHT_TYPE.match(/LightDisc\s*<(.+?),(.*)>/);
    direction = groups[1].trim();
    leds = groups[2].split(",").map((x) => parseInt(x.trim()));
}
let ledCounts = leds.reduce((a, b) => a + b, 0);

studio.extend({
    id: 'rgblight-extension',
    toolbars: {
        global(set, studio) {
            set([
                {
                    type: 'Icon',
                    title: 'Save',
                    svgSource: '💾',
                    onClick: () => saveAnim(projectId)
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
                node.innerHTML = `
                    <div style="width: 100%; height: 100%; display: flex; justify-content: center; align-items: center;">
                        <div id="leds" style="display: flex; flex-direction: row; gap: 5px;">
                            ${Array(ledCounts).fill().map((_, i) => `<div style="width: 20px; height: 20px; background-color: white;"></div>`).join("")}
                        </div>
                    </div>
                `;
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

async function saveAnim(name) {
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
        const sequence = getProject(name).sheet("Light Animation").sequence;
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
    const project = getProject(name, { state });
    projectId = project.address.projectId;
    const sheet = project.sheet("Light Animation");

    objs = [];
    const ledElements = document.getElementById("theatrejs-studio-root").shadowRoot.getElementById("leds").children;
    for (let i = 0; i < ledCounts; i++) {
        const objName = `LED ${i + 1}`;
        let obj = sheet.__experimental_getExistingObject(objName);
        if (!obj) {
            obj = sheet.object(`LED ${i + 1}`, {
                color: types.rgba({ r: 1, g: 1, b: 1, a: 1 }),
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
                const propAdress = {...root.address, pathToProp: path};
                stateEditors.coreByProject.historic.sheetsById.sequence.setPrimitivePropAsSequenced(
                    propAdress,
                );
            }, false);
        }
    }

    hideToast();
}
