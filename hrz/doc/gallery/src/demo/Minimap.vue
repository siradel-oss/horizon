<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel/horizon-api";
import { HrzProtocol } from "@siradel/horizon-protocol";
import { applyDefaultOrthoBaseLayer } from "@/utils/scenes";
import { ref } from "vue";
import FullscreenSource from "@/component/FullscreenSource.vue";

let minimapCanvas = ref<HTMLCanvasElement | null>(null);

let minimapImage = new Image(360, 180);
minimapImage.src = "assets/demo/minimap.png";

async function drawMinimap(api: HrzApi.AsyncApi) {
    let minimapCtx = minimapCanvas.value?.getContext("2d");
    if (!minimapCtx) return;

    let bbox = await api.CameraService.getSceneViewViewBox({
        sceneView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
    });
    let poly = await api.CameraService.getSceneViewViewPolygon({
        sceneView: HrzProtocol.SceneViewIndex.SCENE_VIEW_0,
    });

    let center = poly.center?.longitude || 0;

    let xOffset = -center;
    if (xOffset > 0) {
        xOffset -= 360;
    }

    // Handle the antimeridian here.
    // There is also the "crossesAntimeridian" boolean than can help.
    let longitudeToX = function (lon: number) {
        var x = lon + 180 - center;
        if (x > 360) return x - 360;
        else if (x < 0) return x + 360;
        else return x;
    };

    minimapCtx.globalAlpha = 1.0;
    minimapCtx.drawImage(minimapImage, xOffset, 0);
    minimapCtx.drawImage(minimapImage, xOffset + 360, 0);

    let widthRect = bbox.east - bbox.west;
    if (widthRect < 0) widthRect += 360;

    let xRect = longitudeToX(bbox.west);
    if (widthRect == 360) {
        // We're around a pole, so since the given bounds would be [-180; 180],
        // recenter the rectangle on the map.
        xRect = 0;
    }

    minimapCtx.fillStyle = "blue";
    minimapCtx.globalAlpha = 0.3;
    minimapCtx.lineWidth = 2;
    minimapCtx.beginPath();
    minimapCtx.rect(xRect, 180 - (bbox.north + 90), widthRect, bbox.north - bbox.south);
    minimapCtx.fill();
    minimapCtx.stroke();

    minimapCtx.fillStyle = "red";
    minimapCtx.beginPath();

    var prevX = 0,
        prevY = 0;
    for (var i = 0; i < poly.points.length; ++i) {
        let pt = poly.points[i];
        let x = longitudeToX(pt?.longitude || 0);
        let y = 180 - ((pt?.latitude || 0) + 90);

        if (i == 0) {
            minimapCtx.moveTo(x, y);
        } else {
            // Handle the poles here.
            // We're about to wrap, so draw the polygon going to the pole
            if (prevX - x > 180) {
                // Because of the winding order, this can only happen for the north pole.
                // There is also the "encompassesNorthPole" boolean.
                minimapCtx.lineTo(x + 360, y);
                minimapCtx.lineTo(x + 360, 0);
                minimapCtx.lineTo(prevX - 360, 0);
                minimapCtx.lineTo(prevX - 360, prevY);
            } else if (x - prevX > 180) {
                // Because of the winding order, this can only happen for the south pole.
                // There is also the "encompassesSouthPole" boolean.
                minimapCtx.lineTo(prevX - 360, prevY);
                minimapCtx.lineTo(prevX - 360, 180);
                minimapCtx.lineTo(x + 360, 180);
                minimapCtx.lineTo(x + 360, y);
            }
            minimapCtx.lineTo(x, y);
        }

        prevX = x;
        prevY = y;
    }
    minimapCtx.closePath();
    minimapCtx.fill();
    minimapCtx.stroke();
}

async function onHorizonReady(api: HrzApi.AsyncApi) {
    applyDefaultOrthoBaseLayer(api);
    setTimeout(() => setInterval(() => drawMinimap(api), 100), 100);
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Minimap</h1>
                <p>
                    In this example, a minimap is drawn using the <code>GetCameraViewBox</code> and
                    <code>GetCameraViewPolygon</code> methods. See the
                    <a href="../camera_controls.html#retrieving-view-information">documentation</a>
                    for more information about how to use these methods and their results.
                </p>
                <p>
                    On the canvas below, the view polygon is displayed in red and the view box in
                    blue.
                </p>
                <div class="flex flex-row justify-center mt-4">
                    <canvas
                        class="rounded-lg shadow-md"
                        ref="minimapCanvas"
                        width="360"
                        height="180"
                    ></canvas>
                </div>
                <p><FullscreenSource file="source/Minimap.vue" /></p>
            </div>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
