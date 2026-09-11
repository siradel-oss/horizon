// SPDX-FileCopyrightText: Copyright 2026 Siradel
// SPDX-License-Identifier: MIT

export function useInstantOrContinuousAction<Args extends any[]>(
    instantAction: (...args: Args) => void,
    startContinuousAction: (...args: Args) => void,
    stopContinuousAction: () => void,
    continuousActionDelay: number = 140
) {
    let continuousActive = false;
    let continuousActionTimer: number | null = null;
    let latestArgs: Args | null = null;

    const onMouseDown = (...args: Args) => {
        latestArgs = args;

        if (continuousActive) {
            stopContinuousAction();
            continuousActive = false;
        }

        if (continuousActionTimer !== null) {
            clearTimeout(continuousActionTimer);
            continuousActionTimer = null;
        }

        continuousActionTimer = window.setTimeout(() => {
            startContinuousAction(...latestArgs!);
            continuousActionTimer = null;
            latestArgs = null;
            continuousActive = true;
        }, continuousActionDelay);
    };

    const onMouseUp = () => {
        if (continuousActionTimer !== null) {
            clearTimeout(continuousActionTimer);
            continuousActionTimer = null;
            instantAction(...latestArgs!);
        } else if (continuousActive) {
            stopContinuousAction();
            continuousActive = false;
        }
    };

    return {
        onMouseDown,
        onMouseUp,
    };
}
