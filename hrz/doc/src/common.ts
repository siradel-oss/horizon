import { HrzApi } from "@siradel/horizon-api";
import { HrzCoreBackend } from "@siradel/horizon-core";
import { HrzProtocol } from "@siradel/horizon-protocol";
import Vue from "vue";

let resourceMap = {};
resourceMap["https://dev.virtualearth.net/Branding/logo_powered_by.png"] =
    "data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAEAAAAAVCAYAAAD2KuiaAAAACXBIWXMAAAsTAAALEwEAmpwYAAAAAXNSR0IArs4c6QAAAARnQU1BAACxjwv8YQUAAARDSURBVHgB7VjNjhpHEK7qYcEoh+B7JI/yAkuOPi0+57DknMOwkXIGy4JhwRJYCrsLewicc2B4AsMTePYJTJ5gZ5/AOIcYGKYrVT2wwrsBK4pmEyv5JJju6gK6quvna/C3H78m2As8+aX0lQMIhd0q4L14enVyV1ytN0pAZIeLD33GbFuOmnK93nm/Vm+0KILJ5eXZFBJE7bTpoIY+73Xcvejc7lVBglCAjkLVSqezpY3Mdds2y4eoVFnmCFhQCnOQNDRUiOCVGO+6L19Xq9W8iFOQPAI29piffZkQhm1EmAJBLp7DBGkVyNh13QKgVdREM4mag0y2okD5GnThsnve3l5XkPG63XZQrTbyyoKSyETHfE/jZRk05fm7/d55Z2QijsDmEzmsuqesQwVlpWeu2xwlGgHGQK0HbGyeN2/LHAGOeLODzToScSSkbJMukHptZKiMrkQPKwxlLqnCxg9j461DwPBNpVLJKQs/+kyt3vQ47dgh+oa/u+3Wm2/27S95Byjk3Ec+RVWKjYRAURTAvY2gowEG3YuzSu+iU9rUjJSFZs6hUiRNAznlxSJ1IhGUzX5xHEcSfol00K5U2jl2sKNXyxMTDYSS64Vw/vuYHTlj3ZGRr8fdbsdP3AFrNww4lMtIWFZcMHfo2Eh6dle6WsF7eSJSLnYm51K/bZ5KAT9X35AJrPBtJvMhH8uVWdf6YAafwIM4QDzNW5yKEeeck3+mw6E9ksIoeV6rnRYlvD9SIJgoRLPOufuznOJySTekUkdcQ7xYSUoaBfw0ekqtWizwtzvQFo5N/YAEIQVOEQZmzLWANK5zPxuwQeO12kTrdQHjsARMtRAtZ73myZoMFot0m6v4r7LOB27rCL4Lw3SAGouxjNNHHE3RM/7dx0aG+gZodbL9OzLgcHnFb3k+EBvf/WCX9lqRBt/73rY1chXdAW1BUH3q+/AZIvX427fOXg2Ed6SfPOMzPNypE+FVlUMNPkOk9jI8AUUjYzziHj3Ju/uo1+ulWw165EvffuE2CykEZ5uN/ZNItgaANeTXkQaLCUx47YrxsAq4E/jwL0HyTJALW4+LExOSJ8LoFGS9KFqY1iYREkVqCoqK3AGue+sOIRWceUNB2B7ivDCfz8c7KvnfRvIO4HB36w2u1JBTpD2Ny4JheABjiRCmsT437jFE1OcIudFcmXmtxb28DzpssU4pk8n4LEvEAQ/CA4S+chHIEanivbVo+bx79tMgZosSIegI4xPZYn7wHBJG8g5Y009DdCy813E2rI0wen/7kTuML0kkngKkdJ5z3SZQDkfB5FP6fIm5YqeU+bo6VSrtQMKQNujt1UAr4LfJrlZnQDuruoekDs0/Lgr73bPOQDoBzzYs0FssFjGvlxQgobcZP/0otJXKtIhbMN/yKvBfgtzoNmO513MBvYYE8RB/iPwlpNMruewMzYQoEM4P/yM5/AGjzhm5fw/A7QAAAABJRU5ErkJggg==";

export function $(id: string): HTMLElement {
    return document.getElementById(id);
}

export async function setCameraPosition(
    api: HrzApi.AsyncApi,
    vp: HrzProtocol.IAngularViewpoint,
    cam: HrzProtocol.CameraIndex = HrzProtocol.CameraIndex.CAMERA_0
) {
    await api.CameraService.setOrbit({
        cameraIndex: cam,
        angularViewpoint: vp,
        limitBounds: {
            east: 180,
            north: 90,
            south: -90,
            west: -180,
        },
        maxAltitude: 10000000,
        minTilt: 0,
        maxTilt: 3.14159,
        goToAnimation: {
            duration: 0,
        },
        isInterruptible: true,
        altitudeMode: HrzProtocol.AltitudeMode.RELATIVE_TO_ELLIPSOID,
        correctionAnimation: {
            duration: 0,
        },
    });
}

export type MessageHandlerFn = (msg: HrzProtocol.ITypedMessage) => void;
export type RegisterMessageHandlerFn = (handler: MessageHandlerFn) => void;

export function initExample(
    canvasId: string,
    initScene: (api: HrzApi.AsyncApi, registerHandler: RegisterMessageHandlerFn) => Promise<void>
) {
    let canvas = $(canvasId) as HTMLCanvasElement;
    let attributionsEl = null;

    let section = canvas as HTMLElement;
    while (section && section.id != canvasId + "-section") {
        section = section.parentElement;
    }

    if (section) {
        const fullscreenInHtml = '<img src="static/fullscreen-in.svg" />';
        const fullscreenOutHtml = '<img src="static/fullscreen-out.svg" />';

        section.style["background-color"] = "hsl(var(--bg-color))";
        section.style["display"] = "flex";
        section.style["flex-direction"] = "column";
        canvas.style["flex-grow"] = 1;

        let fullscreenButton = document.createElement("button");
        fullscreenButton.innerHTML = fullscreenInHtml;
        fullscreenButton.classList.add("fullscreen-button");
        fullscreenButton.style["position"] = "absolute";
        fullscreenButton.style["top"] = "0.5em";
        fullscreenButton.style["right"] = "0.5em";
        fullscreenButton.style["width"] = "2em";
        fullscreenButton.style["height"] = "2em";
        fullscreenButton.style["padding"] = "0.1em";
        fullscreenButton.style["display"] = "flex";
        fullscreenButton.style["justify-content"] = "center";
        fullscreenButton.style["align-items"] = "center";

        let wrapperDiv = document.createElement("div");
        wrapperDiv.style["position"] = "relative";
        wrapperDiv.style["display"] = "flex";
        wrapperDiv.style["flex-grow"] = 1;
        wrapperDiv.style["flex-direction"] = "column";

        attributionsEl = document.createElement("p");
        attributionsEl.classList.add("attributions");
        attributionsEl.style["display"] = "none";

        canvas = canvas.parentNode.replaceChild(wrapperDiv, canvas) as HTMLCanvasElement;
        wrapperDiv.appendChild(canvas);
        wrapperDiv.appendChild(attributionsEl);
        wrapperDiv.appendChild(fullscreenButton);

        let isFullscreen = false;
        fullscreenButton.onclick = async function () {
            if (!isFullscreen) {
                await section.requestFullscreen();
            } else {
                await document.exitFullscreen();
            }
        };

        document.addEventListener("fullscreenchange", () => {
            isFullscreen = document["fullscreenElement"] === section;

            if (isFullscreen) {
                fullscreenButton.innerHTML = fullscreenOutHtml;
                section.style["padding"] = "1em";
            } else {
                fullscreenButton.innerHTML = fullscreenInHtml;
                section.style["padding"] = "0";
            }
        });
    }

    let options: HrzProtocol.IViewerOptions = {
        keyBindings: {
            bindings: [
                {
                    key: HrzProtocol.Key.K_R,
                    action: HrzProtocol.KeyAction.RESET_NORTH,
                },
                {
                    key: HrzProtocol.Key.K_P,
                    action: HrzProtocol.KeyAction.TOGGLE_DEV_UI,
                },
                {
                    key: HrzProtocol.Key.K_O,
                    action: HrzProtocol.KeyAction.MOVE_DEV_UI,
                },
                {
                    key: HrzProtocol.Key.K_D,
                    action: HrzProtocol.KeyAction.DESELECT_ALL,
                },
                {
                    key: HrzProtocol.Key.K_A,
                    action: HrzProtocol.KeyAction.EDITOR_APPEND,
                },
                {
                    key: HrzProtocol.Key.K_S,
                    action: HrzProtocol.KeyAction.EDITOR_SELECT,
                },
                {
                    key: HrzProtocol.Key.K_DELETE,
                    action: HrzProtocol.KeyAction.EDITOR_DELETE_SELECTED_POINT,
                },
                {
                    key: HrzProtocol.Key.K_M,
                    action: HrzProtocol.KeyAction.TOGGLE_MONITORING,
                },
                {
                    key: HrzProtocol.Key.K_CTRL,
                    action: HrzProtocol.KeyAction.MOD_KEY,
                },
            ],
        },
        showLoadingScreen: true,
    };

    function displayAttributions(attributions: HrzProtocol.IAttribution[]) {
        var text = " ";
        for (let attribution of attributions) {
            let logoUrl = attribution.logoUrl;
            if (logoUrl in resourceMap) {
                logoUrl = resourceMap[logoUrl];
            }

            if (logoUrl.length > 0) {
                text += ` <img src="${logoUrl}" crossorigin="anonymous" />`;
            }

            if (text.length > 0) {
                text += " " + attribution.text;
            }

            if (logoUrl.length > 0 || text.length > 0) {
                text += ",";
            }
        }
        attributionsEl.innerHTML = text.substring(0, text.length - 1).trim();
        if (attributionsEl.innerHTML.length > 0) {
            attributionsEl.style["display"] = "block";
        } else {
            attributionsEl.style["display"] = "none";
        }
    }

    async function onReady(api: HrzApi.AsyncApi) {
        let handlers: MessageHandlerFn[] = [];

        function registerHandler(handler: MessageHandlerFn) {
            handlers.push(handler);
        }

        async function checkMessages() {
            while (true) {
                let messages = await api.MessageQueueService.dequeueMessages({
                    maxMessageCount: 100,
                });
                for (let msg of messages.messages) {
                    if (
                        msg.type == HrzProtocol.MessageType.ATTRIBUTIONS_MESSAGE &&
                        msg.attributions
                    ) {
                        displayAttributions(msg.attributions.attributions);
                    }
                    for (let handler of handlers) {
                        handler(msg);
                    }
                }
                if (messages.queueSize.messageCount == 0) break;
            }
        }

        await api.ViewerService.setAttributionEnabled({ value: true });
        setInterval(checkMessages, 50);
        setTimeout(() => initScene(api, registerHandler), 100);
    }

    HrzCoreBackend.init(
        canvas,
        "static/",
        options,
        async function (backend: HrzApi.AsyncBackend, initStatus: HrzProtocol.ViewerInitStatus) {
            if (backend) {
                const api = new HrzApi.AsyncApi(backend);
                let checkReady = async function () {
                    while (true) {
                        let messages = await api.MessageQueueService.dequeueMessages({
                            maxMessageCount: 100,
                        });
                        for (let msg of messages.messages) {
                            if (msg.type == HrzProtocol.MessageType.VIEWER_READY_MESSAGE) {
                                onReady(api);
                                return;
                            }
                        }
                        if (messages.queueSize.messageCount == 0) break;
                    }
                    setTimeout(checkReady, 100);
                };
                await checkReady();
            } else {
                console.error(
                    "Failed to initialize Horizon: " + HrzProtocol.ViewerInitStatus[initStatus]
                );
            }
        }
    );
}

export function rgbToHtml(r: number, g: number, b: number): string {
    r = Math.max(Math.min(Math.floor(r * 255), 255), 0);
    g = Math.max(Math.min(Math.floor(g * 255), 255), 0);
    b = Math.max(Math.min(Math.floor(b * 255), 255), 0);
    return "#" + ((1 << 24) + (r << 16) + (g << 8) + b).toString(16).slice(1);
}

export function htmlToRgb(value: string): { r: number; g: number; b: number } {
    return {
        r: parseInt(value.substring(1, 3), 16) / 255,
        g: parseInt(value.substring(3, 5), 16) / 255,
        b: parseInt(value.substring(5, 7), 16) / 255,
    };
}

Vue.filter("formatNumber", function (value: number): string {
    return value.toFixed(2);
});

export function htmlComputedColor(modelColor: HrzProtocol.IColor): any {
    return {
        get: function (): string {
            return rgbToHtml(modelColor.r, modelColor.g, modelColor.b);
        },
        set: function (value: string) {
            let rgb = htmlToRgb(value);
            modelColor.r = rgb.r;
            modelColor.g = rgb.g;
            modelColor.b = rgb.b;
        },
    };
}

export function strComputedEnum(enumDescriptor: any, object: any, member: string): any {
    return {
        get: function (): string {
            return enumDescriptor[object[member]];
        },
        set: function (value: string) {
            object[member] = enumDescriptor[value];
        },
    };
}
