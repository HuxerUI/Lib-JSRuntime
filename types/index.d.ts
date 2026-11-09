// TypeScript surface for the HuxerUI TSX runtime.
//
// Import host elements and hooks from "@huxerui/tsx". The runtime serves this
// module through QuickJS; nothing here is executed as JavaScript.
//
// tsconfig needs:
//   "jsx": "react-jsx",
//   "jsxImportSource": "@huxerui/tsx",
//   "paths": {
//     "@huxerui/tsx": ["<lib>/types/index.d.ts"],
//     "@huxerui/tsx/jsx-runtime": ["<lib>/types/jsx-runtime.d.ts"]
//   }

export type Key = string | number;

export type Child =
  | VNode
  | string
  | number
  | boolean
  | null
  | undefined
  | Child[];

export interface VNode {
  readonly type: HostTag | Component<never> | typeof Fragment;
  readonly props: Record<string, unknown>;
  readonly key: Key | null;
}

/** A plain tag string the native renderer understands. */
export type HostTag = string;

export interface Component<Props = Record<string, never>> {
  (props: Props): Child;
}

export type HostComponent<Props> = (props: Props) => VNode;

export interface FrameSpec {
  width?: number;
  height?: number;
  minWidth?: number;
  maxWidth?: number;
  minHeight?: number;
  maxHeight?: number;
}

export interface EdgeInsetsSpec {
  left?: number;
  top?: number;
  right?: number;
  bottom?: number;
  horizontal?: number;
  vertical?: number;
}

export type EdgeInsetsValue = number | [number] | [number, number] | [number, number, number, number];

export interface BorderSpec {
  color?: ColorValue;
  width?: number;
}

export interface ShadowSpec {
  color?: ColorValue;
  offset?: [number, number];
  blurRadius?: number;
  spread?: number;
}

export interface AlignSpec {
  horizontal?: "start" | "center" | "end" | "stretch";
  vertical?: "start" | "center" | "end" | "stretch";
}

export type ColorValue = string | number | [number, number, number] | [number, number, number, number] | {
  r: number;
  g: number;
  b: number;
  a?: number;
};

export type MainAlignValue = "start" | "center" | "end" | "spaceBetween" | "spaceAround" | "spaceEvenly";
export type CrossAlignValue = "start" | "center" | "end" | "stretch";

export interface CommonProps {
  key?: Key;
  children?: Child;
  padding?: EdgeInsetsValue | EdgeInsetsSpec;
  spacing?: number;
  frame?: FrameSpec;
  width?: number;
  height?: number;
  background?: ColorValue;
  foreground?: ColorValue;
  border?: ColorValue | BorderSpec;
  cornerRadius?: number | [number];
  shadow?: ShadowSpec;
  clipChildren?: boolean;
  enabled?: boolean;
  focusable?: boolean;
  mainAlign?: MainAlignValue;
  crossAlign?: CrossAlignValue;
  align?: "start" | "center" | "end" | "stretch" | AlignSpec;
  grow?: boolean | number;
  onClick?: () => void;
}

export interface LayoutProps extends CommonProps {}

export interface TextProps extends CommonProps {
  value?: string | number;
  role?: "body" | "label" | "title";
  fontSize?: number;
  fontWeight?: number | "thin" | "extraLight" | "light" | "regular" | "medium" | "semiBold" | "bold" | "extraBold" | "black";
  fontFamily?: string;
  italic?: boolean;
  color?: ColorValue;
  decoration?: "none" | "underline" | "strikeThrough";
  textAlign?: "leading" | "center" | "trailing" | "start" | "end";
}

export interface ButtonProps extends CommonProps {
  label?: string;
}

export interface IconButtonProps extends CommonProps {
  src: string;
  label: string;
}

export interface ImageProps extends CommonProps {
  src: string;
  fit?: "none" | "contain" | "cover" | "fill" | "scaleDown";
  tint?: ColorValue;
}

export interface DividerProps extends CommonProps {
  axis?: "horizontal" | "vertical";
}

export interface ScrollViewProps extends CommonProps {
  axis?: "horizontal" | "vertical";
}

export interface TextFieldProps extends CommonProps {
  value?: string;
  label?: string;
  placeholder?: string;
  variant?: "filled" | "outlined" | "standard";
  multiline?: boolean;
  maxLength?: number;
  secure?: boolean;
  onChange?: (value: string) => void;
  onSubmit?: () => void;
}

export interface ToggleProps extends CommonProps {
  label?: string;
  checked?: boolean;
  selected?: boolean;
  onChange?: (value: boolean) => void;
}

export interface SliderProps extends CommonProps {
  value?: number;
  min?: number;
  max?: number;
  step?: number;
  onChange?: (value: number) => void;
}

export interface ProgressProps extends CommonProps {
  progress?: number;
}

export interface ChipProps extends CommonProps {
  label?: string;
  selected?: boolean;
  onChange?: (value: boolean) => void;
}

export interface IndexedPagesProps extends CommonProps {
  selected?: number;
}

export interface ThemeProps extends CommonProps {}

export const Fragment: Component<{ children?: Child }>;

export const Column: HostComponent<LayoutProps>;
export const Row: HostComponent<LayoutProps>;
export const Flow: HostComponent<LayoutProps>;
export const Stack: HostComponent<LayoutProps>;
export const Theme: HostComponent<ThemeProps>;
export const Text: HostComponent<TextProps>;
export const Button: HostComponent<ButtonProps>;
export const IconButton: HostComponent<IconButtonProps>;
export const Image: HostComponent<ImageProps>;
export const Divider: HostComponent<DividerProps>;
export const Spacer: HostComponent<CommonProps>;
export const ScrollView: HostComponent<ScrollViewProps>;
export const Checkbox: HostComponent<ToggleProps>;
export const RadioButton: HostComponent<ToggleProps>;
export const Switch: HostComponent<ToggleProps>;
export const Slider: HostComponent<SliderProps>;
export const ProgressBar: HostComponent<ProgressProps>;
export const ProgressCircle: HostComponent<ProgressProps>;
export const TextField: HostComponent<TextFieldProps>;
export const Chip: HostComponent<ChipProps>;
export const IndexedPages: HostComponent<IndexedPagesProps>;
export const SelectionArea: HostComponent<CommonProps>;

export function mount(root: Child | Component): void;

export type StateSetter<T> = (next: T | ((previous: T) => T)) => void;

export function useState<T>(initial: T | (() => T)): [T, StateSetter<T>];
export function useReducer<State, Action>(
  reducer: (state: State, action: Action) => State,
  initialArg: State,
  init?: (value: State) => State,
): [State, (action: Action) => void];
export function useRef<T>(initial: T): { current: T };
export function useMemo<T>(factory: () => T, deps?: readonly unknown[]): T;
export function useCallback<T extends (...args: never[]) => unknown>(callback: T, deps?: readonly unknown[]): T;
export function useEffect(effect: () => void | (() => void), deps?: readonly unknown[]): void;
export function useLayoutEffect(effect: () => void | (() => void), deps?: readonly unknown[]): void;

/** Classic JSX factory for `"jsx": "react"` + `"jsxFactory": "h"`. */
export function h(type: unknown, props?: Record<string, unknown> | null, ...children: Child[]): VNode;
export const createElement: typeof h;
