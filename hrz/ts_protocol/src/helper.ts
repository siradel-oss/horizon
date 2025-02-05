import { HrzProtocol } from "./hrz_protocol";
import * as Long from "long";

export class HrzProtocolHelper {
    public static readonly SCENE_VIEW_COUNT: number = Object.keys(HrzProtocol.SceneViewIndex)
        .length;

    public static readonly CAMERA_COUNT: number = Object.keys(HrzProtocol.CameraIndex).length;

    public static attributeAsNumber(value: HrzProtocol.IAttributeValue): number {
        if (value.numberValue !== undefined && value.numberValue !== null) {
            return value.numberValue;
        } else if (value.uint64Value !== undefined && value.uint64Value !== null) {
            if (value.uint64Value instanceof Long) {
                return (value.uint64Value as Long).toNumber();
            } else {
                return value.uint64Value as number;
            }
        } else if (value.int64Value !== undefined && value.int64Value !== null) {
            if (value.int64Value instanceof Long) {
                return (value.int64Value as Long).toNumber();
            } else {
                return value.int64Value as number;
            }
        } else if (value.stringValue !== undefined && value.stringValue !== null) {
            return Number.parseFloat(value.stringValue);
        } else if (value.booleanValue !== undefined && value.booleanValue !== null) {
            return value.booleanValue ? 1 : 0;
        } else {
            return 0;
        }
    }

    public static attributeAsAny(
        value: HrzProtocol.IAttributeValue
    ): number | Long | string | boolean | null {
        if (value.numberValue !== undefined && value.numberValue !== null) {
            return value.numberValue;
        } else if (value.uint64Value !== undefined && value.uint64Value !== null) {
            return value.uint64Value;
        } else if (value.int64Value !== undefined && value.int64Value !== null) {
            return value.int64Value;
        } else if (value.stringValue !== undefined && value.stringValue !== null) {
            return value.stringValue;
        } else if (value.booleanValue !== undefined && value.booleanValue !== null) {
            return value.booleanValue;
        } else {
            return null;
        }
    }
}
