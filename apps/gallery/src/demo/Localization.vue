<!--
    SPDX-FileCopyrightText: Copyright 2025 Siradel
    SPDX-License-Identifier: MIT
-->

<script setup lang="ts">
import SplitView from "@/layout/SplitView.vue";
import Viewer from "@/component/Viewer.vue";
import { HrzApi } from "@siradel-oss/horizon-api";
import { HrzProtocol } from "@siradel-oss/horizon-protocol";
import { applyScene, getLayerByName } from "@/utils/scenes";
import FullscreenSceneModel from "@/component/FullscreenSceneModel.vue";
import FullscreenSource from "@/component/FullscreenSource.vue";
import { ref, watch, computed } from "vue";
import { debounce } from "@/utils/utils";
import { shallowRef } from "vue";

let api = shallowRef<HrzApi.AsyncApi | null>(null);
let dataLayer = shallowRef<HrzProtocol.LayerHandle | null>(null);
let tileLayer = shallowRef<HrzProtocol.LayerHandle | null>(null);
let selectedLang = ref<string>("en");
let showEnglish = ref<boolean>(false);

interface Language {
    code: string;
    name: string;
}

let LANGUAGES: Language[] = [
    { code: "bg", name: "Bulgarian" },
    { code: "hr", name: "Croatian" },
    { code: "cs", name: "Czech" },
    { code: "da", name: "Danish" },
    { code: "nl", name: "Dutch" },
    { code: "en", name: "English" },
    { code: "et", name: "Estonian" },
    { code: "fi", name: "Finnish" },
    { code: "fr", name: "French" },
    { code: "de", name: "German" },
    { code: "el", name: "Greek" },
    { code: "hu", name: "Hungarian" },
    { code: "ga", name: "Irish" },
    { code: "it", name: "Italian" },
    { code: "lv", name: "Latvian" },
    { code: "lt", name: "Lithuanian" },
    { code: "mt", name: "Maltese" },
    { code: "no", name: "Norwegian" },
    { code: "pl", name: "Polish" },
    { code: "pt", name: "Portuguese" },
    { code: "ro", name: "Romanian" },
    { code: "ru", name: "Russian" },
    { code: "sk", name: "Slovak" },
    { code: "sl", name: "Slovenian" },
    { code: "es", name: "Spanish" },
    { code: "sv", name: "Swedish" },
    { code: "tr", name: "Turkish" },
    { code: "uk", name: "Ukrainian" },
];

const stylingScript = computed(() => {
    let script = `
        if (min(6, attr("min_zoom")) > uniform("tile_z")) { discard; }


        if (not is_null(attr("loc_name"))) {
    `;

    if (showEnglish.value) {
        script += `
            if (attr("loc_name") != value_or(attr("en_name"), attr("name"))) {
                set "name" = attr("loc_name");
                set "sub_name" = value_or(attr("en_name"), attr("name"));
            } else {
                set "name" = attr("loc_name");
            }
        `;
    } else {
        script += `
            set "name" = attr("loc_name");
        `;
    }

    script += `
        } else {
            set "name" = value_or(attr("en_name"), attr("name"));
        }

        if (attr("place") == "country") {
            emit "country";
        } elif (not is_null(attr("place"))) {
            emit "region";
        } else {
            set "font_size" = add(5, mul(1.1, sub(10, attr("min_zoom"))));
            set "sub_font_size" = add(4, mul(1.1, sub(10, attr("min_zoom"))));
            set "symbol_size_x" = add(5.5, mul(0.5, sub(10, attr("min_zoom"))));
            set "symbol_size_y" = prp("symbol_size_x");
            set "symbol_padding" = mul(0.75, prp("symbol_size_x"));
            set "symbol_border_size" = add(0.9, mul(0.1, sub(10, attr("min_zoom"))));
            set "cull_priority" = sub(10, attr("min_zoom"));
            emit "locality";
        }
    `;

    return script;
});

watch(
    [selectedLang, dataLayer, api],
    debounce(async () => {
        if (dataLayer.value) {
            await HrzApi.VectorDataLayerPathBuilder.create(dataLayer.value)
                .sources(1)
                .pmtilesDataProvider()
                .url()
                .set(api.value!, `assets/demo/hrz-basemap-l10n/${selectedLang.value}.pmtiles`);
            await HrzApi.VectorDataLayerPathBuilder.create(dataLayer.value)
                .sources(1)
                .attributes(0)
                .sourceName()
                .set(api.value!, `name:${selectedLang.value}`);
        }
    }, 100)
);

watch(
    [stylingScript, tileLayer, api],
    debounce(async () => {
        if (tileLayer.value) {
            await HrzApi.VectorTilesLayerPathBuilder.create(tileLayer.value)
                .style()
                .stylingScript()
                .set(api.value!, stylingScript.value);
        }
    }, 100)
);

async function onHorizonReady(api_: HrzApi.AsyncApi) {
    api.value = api_;
    await applyScene(api.value, "map_localization").then(async function () {
        dataLayer.value = (await getLayerByName(api.value!, "HRZ Basemap places data")) || null;
        tileLayer.value = (await getLayerByName(api.value!, "HRZ Basemap places")) || null;
    });
}

async function retrieveVectorDataLayerModel(): Promise<any> {
    if (!dataLayer.value) {
        return {};
    }
    return (
        await HrzApi.VectorDataLayerPathBuilder.create(dataLayer.value).get(api.value!)
    ).toJSON();
}

async function retrieveVectorTilesLayerModel(): Promise<any> {
    if (!tileLayer.value) {
        return {};
    }
    return (
        await HrzApi.VectorTilesLayerPathBuilder.create(tileLayer.value).get(api.value!)
    ).toJSON();
}
</script>
<template>
    <SplitView>
        <template #left>
            <div class="typography-normal">
                <h1>Map localisation</h1>
                <p>
                    Vector data is rendered in real time according to the configuration of the
                    scene. This gives the ability to localise the map in different languages when
                    the data is available.
                </p>
                <p>
                    This demo shows an example of a map whose labels can be localised. It is also
                    possible to display the English name of any localised label if the localised
                    name is different from the English one.
                </p>
                <p>
                    Contrarily to what would have to be done with a raster tileset, it is not
                    necessary to generate a tileset for each language. And thanks to the capacity of
                    Horizon to join multiple vector data sources, there is no need to package all
                    the localised names in a single vector dataset. Instead the main dataset
                    contains geometries and English names only, while the localised names are stored
                    in a separate dataset for each language. This allows loading just what is
                    needed.
                </p>
                <p>
                    Having separate dataset also simplifies the generation and maintenance of the
                    data. It enables adding new languages without regenerating the whole dataset, or
                    updating the geometries and names separately.
                </p>
                <p>
                    Both the main dataset and the localised name-only datasets identify each feature
                    with a unique numerical identifier. This allows joining the datasets without
                    having to care about the order of the features in each tile.
                </p>
                <hr />
            </div>
            <div class="my-6">
                <p>
                    <label>Language</label><br />
                    <select v-model="selectedLang">
                        <option v-for="(lang, index) in LANGUAGES" :value="lang.code">
                            {{ lang.name }}
                        </option>
                    </select>
                    <br />
                    <br />
                    <label>
                        <input
                            type="checkbox"
                            v-model="showEnglish"
                            :disabled="selectedLang == 'en'"
                        />
                        Show English names below localised names
                    </label>
                </p>
            </div>
            <hr />
            <p>
                <FullscreenSource text="View demo source" file="source/Localization.vue" />
                <FullscreenSceneModel
                    text="View label vector data layer model for labels"
                    :retrieveData="retrieveVectorDataLayerModel"
                />
                <FullscreenSceneModel
                    text="View label vector tile layer model for labels"
                    :retrieveData="retrieveVectorTilesLayerModel"
                />
            </p>
        </template>
        <template #right>
            <Viewer @ready="onHorizonReady" />
        </template>
    </SplitView>
</template>
