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

export interface InputModifiers {
  shift: boolean;
  control: boolean;
  alt: boolean;
  meta: boolean;
}

export type PointerDevice = "mouse" | "touch" | "pen";
export type PointerPhase = "down" | "move" | "up" | "cancel";
export type PointerButtonName = "none" | "primary" | "secondary" | "middle" | "back" | "forward";

export interface PointerInputEvent {
  type: PointerPhase;
  pointerId: number;
  x: number;
  y: number;
  device: PointerDevice;
  button: PointerButtonName;
  /** Bitmask of currently pressed pointer buttons. */
  buttons: number;
  modifiers: InputModifiers;
}

export type HoverPhase = "enter" | "move" | "leave";

export interface HoverInputEvent {
  type: HoverPhase;
  pointerId: number;
  x: number;
  y: number;
  windowX: number;
  windowY: number;
  device: PointerDevice;
}

export interface ScrollInputEvent {
  x: number;
  y: number;
  deltaX: number;
  deltaY: number;
  modifiers: InputModifiers;
}

export type KeyPhase = "down" | "up";

export interface KeyInputEvent {
  type: KeyPhase;
  key: string;
  text: string;
  repeat: boolean;
  modifiers: InputModifiers;
}

export interface ContextMenuInputEvent {
  x: number;
  y: number;
}

export interface DragInputEvent {
  pointerId: number;
  device: PointerDevice;
  origin: { x: number; y: number };
  position: { x: number; y: number };
  windowPosition: { x: number; y: number };
  delta: { x: number; y: number };
  translation: { x: number; y: number };
  velocity: { x: number; y: number };
}

export interface LongPressInputEvent {
  pointerId: number;
  device: PointerDevice;
  position: { x: number; y: number };
  windowPosition: { x: number; y: number };
}

export interface MultiTapInputEvent extends LongPressInputEvent {
  count: number;
}

export interface SemanticsSpec {
  role?: "generic" | "text" | "heading" | "image" | "button" | "link" | "checkbox"
    | "radioButton" | "switch" | "slider" | "progressIndicator" | "comboBox" | "textField"
    | "searchField" | "tab" | "tabList" | "menu" | "menuItem" | "dialog" | "navigation"
    | "list" | "listItem" | "grid" | "gridCell" | "scrollView" | "tree" | "treeItem"
    | "separator";
  label?: TextValue;
  value?: TextValue;
  placeholder?: TextValue;
  hint?: TextValue;
  error?: TextValue;
  stateDescription?: TextValue;
  identifier?: string;
  checked?: boolean | "checked" | "unchecked" | "mixed";
  selected?: boolean;
  expanded?: boolean;
  busy?: boolean;
  readOnly?: boolean;
  required?: boolean;
  invalid?: boolean;
  headingLevel?: number;
  descendants?: "preserve" | "exclude";
  liveRegion?: "none" | "polite" | "assertive";
  hidden?: boolean;
}

export interface FileDropSpec {
  extensions?: readonly string[];
  contentTypes?: readonly string[];
  allowsMultiple?: boolean;
}

export interface FileDropPoint {
  x: number;
  y: number;
  windowX: number;
  windowY: number;
}

export interface FileDropOffer {
  count?: number;
  contentTypes: string[];
}

export interface FileReferenceInfo {
  name: string;
  size?: number;
  contentType?: string;
  canWrite: boolean;
  type: "file" | "directory" | "other";
  /** Native path when the platform provides one; not a portable access grant. */
  path?: string;
}

export interface FileDropError {
  code: "notFound" | "permissionDenied" | "notDirectory" | "isDirectory" | "tooLarge"
    | "invalidEncoding" | "unsupported" | "io" | "alreadyExists" | "timeout" | "noSpace";
  message: string;
}

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
  tooltip?: string;
  onClick?: () => void;
  onPointer?: (event: PointerInputEvent) => void;
  onPointerDown?: (event: PointerInputEvent) => void;
  onPointerMove?: (event: PointerInputEvent) => void;
  onPointerUp?: (event: PointerInputEvent) => void;
  onPointerCancel?: (event: PointerInputEvent) => void;
  onPointerIntercept?: (event: PointerInputEvent) => boolean;
  onHover?: (event: HoverInputEvent) => void;
  onPointerEnter?: (event: HoverInputEvent) => void;
  onPointerLeave?: (event: HoverInputEvent) => void;
  onScroll?: (event: ScrollInputEvent) => boolean;
  onKeyDown?: (event: KeyInputEvent) => boolean;
  onKeyUp?: (event: KeyInputEvent) => boolean;
  onKeyIntercept?: (event: KeyInputEvent) => boolean;
  onFocus?: () => void;
  onBlur?: () => void;
  onFocusChanged?: (focused: boolean) => void;
  onContextMenu?: (event: ContextMenuInputEvent) => void;
  onBack?: () => void;
  onDragStart?: (event: DragInputEvent) => void;
  onDrag?: (event: DragInputEvent) => void;
  onDragEnd?: (event: DragInputEvent) => void;
  onDragCancel?: (event: DragInputEvent) => void;
  dragAxis?: "horizontal" | "vertical";
  /** Minimum movement in logical pixels before drag recognition. */
  dragThreshold?: number;
  /** Press duration in seconds before delayed drag recognition. */
  dragDelay?: number;
  onLongPressStart?: (event: LongPressInputEvent) => void;
  onLongPressEnd?: (event: LongPressInputEvent) => void;
  onLongPressCancel?: (event: LongPressInputEvent) => void;
  longPressDuration?: number;
  longPressMovement?: number;
  onMultiTap?: (event: MultiTapInputEvent) => void;
  multiTapCount?: number;
  multiTapInterval?: number;
  multiTapMovement?: number;
  /** Anchors a usePopup() to this host element. */
  popupAnchor?: PopupRef;
  /** Anchors a useMenu() to this host element. */
  menuAnchor?: MenuRef;
  /** Native accessibility metadata applied to this view. */
  semantics?: SemanticsSpec;
  /** Receives external file drops; true accepts any ordinary file. */
  fileDrop?: boolean | FileDropSpec;
  onFileDrop?: (files: readonly FileReferenceInfo[], event: FileDropPoint) => void;
  onFileDropEnter?: (offer: FileDropOffer, event: FileDropPoint) => void;
  onFileDropMove?: (offer: FileDropOffer, event: FileDropPoint) => void;
  onFileDropExit?: (offer: FileDropOffer, event: FileDropPoint) => void;
  onFileDropFailed?: (error: FileDropError, event: FileDropPoint) => void;
  /** Native focus target; bind to one focusable host element. */
  ref?: FocusRef;
  /** Alias for `ref`. */
  focusRef?: FocusRef;
}

export interface LayoutProps extends CommonProps {}

export interface LocalizedTextSpec {
  /** Packaged resource in "domain:key" form. */
  resource?: string;
  domain?: string;
  key?: string;
}

export type TextValue = string | number | LocalizedTextSpec;

export interface ImageSourceSpec {
  /** Local raster file; equivalent to passing the path as a string. */
  file?: string;
  /** Packaged image resource in "domain:key" form. */
  resource?: string;
  domain?: string;
  key?: string;
}

export type ImageSource = string | ImageSourceSpec;

export type ThemeVariant =
  | "material"
  | "materialDark"
  | "flat"
  | "flatDark";

export interface TextProps extends CommonProps {
  value?: TextValue;
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
  label?: TextValue;
}

export interface IconButtonProps extends CommonProps {
  src: ImageSource;
  label: TextValue;
}

export interface ImageProps extends CommonProps {
  src: ImageSource;
  fit?: "none" | "contain" | "cover" | "fill" | "scaleDown";
  tint?: ColorValue;
}

export interface DividerProps extends CommonProps {
  axis?: "horizontal" | "vertical";
}

export interface SelectProps extends CommonProps {
  items: readonly Child[];
  selected?: number;
  label?: TextValue;
  validation?: ValidationSpec;
  onChange?: (index: number) => void;
  onExpandedChange?: (expanded: boolean) => void;
}

export interface ComboBoxProps extends CommonProps {
  value?: string;
  suggestions: readonly string[];
  label?: TextValue;
  placeholder?: TextValue;
  maxLength?: number;
  validation?: ValidationSpec;
  onChange?: (value: string) => void;
  onSelect?: (index: number, value: string) => void;
  onSubmit?: () => void;
  onExpandedChange?: (expanded: boolean) => void;
}

export interface DatePickerProps extends CommonProps {
  /** Controlled date in YYYY-MM-DD form. */
  value: string;
  min?: string;
  max?: string;
  label?: TextValue;
  validation?: ValidationSpec;
  onChange?: (value: string) => void;
}

export interface TimePickerProps extends CommonProps {
  /** Controlled local time in HH:MM form. */
  value: string;
  /** Selectable minute interval. */
  step?: number;
  label?: TextValue;
  validation?: ValidationSpec;
  onChange?: (value: string) => void;
}

export interface RefreshBoxProps extends CommonProps {
  refreshing?: boolean;
  onRefresh?: () => void;
}

export interface PagerProps extends CommonProps {
  selected?: number;
  axis?: "horizontal" | "vertical";
  reverse?: boolean;
  dragEnabled?: boolean;
  onChange?: (index: number) => void;
  onSettled?: (index: number) => void;
}

export interface SplitChangeSpec {
  index: number;
  grow: number;
  frame?: FrameSpec;
}

export interface SplitLayoutProps extends CommonProps {
  axis?: "horizontal" | "vertical";
  onChange?: (changes: readonly SplitChangeSpec[]) => void;
}

export interface TopAppBarProps extends CommonProps {
  title?: TextValue;
  leading?: Child;
  actions?: readonly Child[];
  titleAlign?: "start" | "center";
}

export type ScrollAlignment = "start" | "center" | "end";

export interface ScrollMetrics {
  offset: number;
  maxOffset: number;
  viewportExtent: number;
  contentExtent: number;
  connected: boolean;
}

export interface ScrollRef {
  readonly id: number;
  scrollTo(offset: number): boolean;
  scrollBy(delta: number): boolean;
  scrollToItem(index: number, alignment?: ScrollAlignment): boolean;
  offset(): number;
  metrics(): ScrollMetrics | undefined;
}

export interface ScrollViewProps extends CommonProps {
  axis?: "horizontal" | "vertical";
  /** Native scroll controller; bind to one ScrollView. */
  scrollRef?: ScrollRef;
}

export interface TextSelectionSpec {
  /** Fixed endpoint in UTF-16 code units. */
  anchor?: number;
  /** Moving endpoint / caret in UTF-16 code units. */
  active?: number;
  affinity?: "upstream" | "downstream";
}

export interface TextRangeSpec {
  start: number;
  end: number;
}

export interface TextEditingInputEvent {
  text: string;
  selection: Required<TextSelectionSpec>;
  composition: TextRangeSpec | null;
}

export interface ValidationSpec {
  status: "none" | "valid" | "invalid" | "pending";
  message?: string;
}

export interface TextFieldProps extends CommonProps {
  value?: string;
  selection?: TextSelectionSpec;
  validation?: ValidationSpec;
  label?: string;
  placeholder?: string;
  variant?: "filled" | "outlined" | "standard";
  multiline?: boolean;
  maxLength?: number;
  secure?: boolean;
  onChange?: (value: string) => void;
  onEdit?: (event: TextEditingInputEvent) => void;
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

export interface VirtualListProps<T = unknown> extends CommonProps {
  items: readonly T[];
  renderItem: (item: T, index: number) => Child;
  axis?: "horizontal" | "vertical";
  itemExtent?: number;
  estimatedItemExtent?: number;
  cacheExtent?: number;
  scrollRef?: ScrollRef;
}

export interface VirtualGridProps<T = unknown> extends CommonProps {
  items: readonly T[];
  renderItem: (item: T, index: number) => Child;
  /** Fixed column count; ignored when adaptiveColumnWidth is positive. */
  columns?: number;
  adaptiveColumnWidth?: number;
  rowExtent?: number;
  estimatedRowExtent?: number;
  rowSpacing?: number;
  columnSpacing?: number;
  cacheExtent?: number;
  itemSpans?: readonly number[];
  scrollRef?: ScrollRef;
}

export interface TabsProps extends CommonProps {
  labels: string[];
  selected?: number;
  onChange?: (index: number) => void;
}

export interface NavigationBarItem {
  label: TextValue;
  src: ImageSource;
  selectedSrc?: ImageSource;
  disabled?: boolean;
}

export interface NavigationBarProps extends CommonProps {
  items: NavigationBarItem[];
  selected?: number;
  onChange?: (index: number) => void;
}

export interface NavigationPaneProps extends CommonProps {
  labels: string[];
  selected?: number;
  expanded?: boolean;
  onChange?: (index: number) => void;
}

export interface SegmentedButtonProps extends CommonProps {
  labels: string[];
  selected?: number;
  onChange?: (index: number) => void;
}

export interface ThemeProps extends CommonProps {
  variant?: ThemeVariant;
  /** Alias for `variant`. */
  theme?: ThemeVariant;
  /** Selects the dark variant of `variant`. */
  dark?: boolean;
}

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
export const Select: HostComponent<SelectProps>;
export const ComboBox: HostComponent<ComboBoxProps>;
export const DatePicker: HostComponent<DatePickerProps>;
export const TimePicker: HostComponent<TimePickerProps>;
export const RefreshBox: HostComponent<RefreshBoxProps>;
export const Pager: HostComponent<PagerProps>;
export const SplitLayout: HostComponent<SplitLayoutProps>;
export const TopAppBar: HostComponent<TopAppBarProps>;
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
export const VirtualList: <T>(props: VirtualListProps<T>) => VNode;
export const VirtualGrid: <T>(props: VirtualGridProps<T>) => VNode;
export const Tabs: HostComponent<TabsProps>;
export const SegmentedButton: HostComponent<SegmentedButtonProps>;
export const NavigationPane: HostComponent<NavigationPaneProps>;
export const NavigationBar: HostComponent<NavigationBarProps>;

export function mount(root: Child | Component): void;

export type StateSetter<T> = (next: T | ((previous: T) => T)) => void;

export function useState<T>(initial: T | (() => T)): [T, StateSetter<T>];
export function useReducer<State, Action>(
  reducer: (state: State, action: Action) => State,
  initialArg: State,
  init?: (value: State) => State,
): [State, (action: Action) => void];
export function useRef<T>(initial: T): { current: T };

export interface PopupOptions {
  dismissOnOutsidePress?: boolean;
  dismissOnCancel?: boolean;
  trapFocus?: boolean;
  retainAnchorFocus?: boolean;
}

export interface PopupRef {
  readonly id: number;
  show(render: () => Child, options?: PopupOptions): number;
  dismiss(layerId: number): boolean;
}
export function usePopup(): PopupRef;

export interface BottomSheetOptions {
  dismissOnOutsidePress?: boolean;
  dismissOnCancel?: boolean;
}

export interface BottomSheetRef {
  readonly id: number;
  show(render: () => Child, options?: BottomSheetOptions): number;
  dismiss(layerId: number): boolean;
}
export function useBottomSheet(): BottomSheetRef;

export interface SnackBarRef {
  readonly id: number;
  show(message: string, duration?: number): number;
  showAction(message: string, action: string, onAction: () => void, duration?: number): number;
  dismiss(layerId: number): boolean;
}
export function useSnackBar(): SnackBarRef;

export interface MenuItemSpec {
  label?: string;
  section?: boolean;
  disabled?: boolean;
  checked?: boolean;
  onSelect?: () => void;
}

export interface MenuRef {
  readonly id: number;
  show(items: readonly MenuItemSpec[]): number;
  showAt(x: number, y: number, items: readonly MenuItemSpec[]): number;
  dismiss(layerId: number): boolean;
}
export function useMenu(): MenuRef;

export interface DialogOptions {
  title?: string;
  message?: string;
  positive?: string;
  negative?: string;
  dismissOnOutsidePress?: boolean;
  dismissOnCancel?: boolean;
  onPositive?: () => void;
  onNegative?: () => void;
}

export interface DialogRef {
  readonly id: number;
  show(options?: DialogOptions): number;
  showContent(render: () => Child, options?: Pick<DialogOptions, "dismissOnOutsidePress" | "dismissOnCancel">): number;
  dismiss(layerId: number): boolean;
}
export function useDialog(): DialogRef;

export interface ToastRef {
  readonly id: number;
  show(message: string, duration?: number): number;
  dismiss(layerId: number): boolean;
}
export function useToast(): ToastRef;

export interface FocusRef {
  readonly id: number;
  requestFocus(): boolean;
}
export function useFocusRef(): FocusRef;
export function useScrollRef(initialOffset?: number): ScrollRef;

export type NavigationPage =
  | VNode
  | ((props?: Record<string, unknown>) => VNode)
  | Child;

export interface NavigationController {
  /** Number of logical pages, including the root page. */
  readonly depth: number;
  readonly canPop: boolean;
  entries(): readonly NavigationPage[];
  current(): NavigationPage | null;
  push(page: NavigationPage): void;
  /** @return False when the stack is already at its root. */
  pop(): boolean;
  replace(page: NavigationPage): void;
  reset(page?: NavigationPage): void;
}

export interface NavigationStackProps extends CommonProps {
  navigation: NavigationController;
}

export function useNavigation(initialPage?: NavigationPage): NavigationController;

/** Renders all retained pages with the top page selected; Back pops the stack. */
export function NavigationStack(props: NavigationStackProps): VNode;

export function useMemo<T>(factory: () => T, deps?: readonly unknown[]): T;
export function useCallback<T extends (...args: never[]) => unknown>(callback: T, deps?: readonly unknown[]): T;
export function useEffect(effect: () => void | (() => void), deps?: readonly unknown[]): void;
export function useLayoutEffect(effect: () => void | (() => void), deps?: readonly unknown[]): void;

/** Opaque timer identifier returned by `setTimeout` and `setInterval`. */
export type TimerHandle = number;
export function setTimeout(callback: () => void, delay?: number): TimerHandle;
export function clearTimeout(handle: TimerHandle): void;
export function setInterval(callback: () => void, delay?: number): TimerHandle;
export function clearInterval(handle: TimerHandle): void;
export function queueMicrotask(callback: () => void): void;

export interface FetchOptions {
  method?: "GET" | "HEAD" | "POST" | "PUT" | "PATCH" | "DELETE" | "OPTIONS" | string;
  headers?: Record<string, string>;
  body?: string;
  /** Complete request deadline in milliseconds. Defaults to 30000. */
  timeout?: number;
}

export interface FetchResponse {
  url: string;
  status: number;
  headers: Record<string, string>;
  body: string;
  byteLength: number;
}

export interface FetchError {
  code: "transport" | "timeout" | "unsupported";
  message: string;
}

/** Sends one HTTP request through the HuxerUI application transport. */
export function fetch(url: string, options?: FetchOptions): Promise<FetchResponse>;

export function readClipboard(): string | null;
export function writeClipboard(text: string): boolean;
export function clipboardAvailable(): boolean;

export interface ClipboardApi {
  read(): string | null;
  write(text: string): boolean;
  available(): boolean;
}

export const clipboard: ClipboardApi;

/** Requests orderly application shutdown. */
export function quitApplication(): void;

/** @return The current application lifecycle state, or "unknown" without a live application. */
export function applicationLifecycle(): "active" | "inactive" | "background" | "unknown";

/** Classic JSX factory for `"jsx": "react"` + `"jsxFactory": "h"`. */
export function h(type: unknown, props?: Record<string, unknown> | null, ...children: Child[]): VNode;
export const createElement: typeof h;
