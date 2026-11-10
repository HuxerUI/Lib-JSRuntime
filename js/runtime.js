// HuxerUI TSX runtime.
//
// This module is served by the native runtime as `@huxerui/tsx` and
// `@huxerui/tsx/jsx-runtime`. It only describes the tree; the C++ bridge turns
// each node into a HuxerUI View and owns the state slots.

const EMPTY_PROPS = Object.freeze({});

function makeNode(type, props, key) {
  const resolvedProps = props === undefined || props === null ? EMPTY_PROPS : props;
  let resolvedKey = key;
  if (resolvedKey === undefined && resolvedProps !== EMPTY_PROPS &&
      Object.prototype.hasOwnProperty.call(resolvedProps, "key")) {
    resolvedKey = resolvedProps.key;
  }
  return {
    type,
    props: resolvedProps,
    key: resolvedKey === undefined ? null : resolvedKey,
  };
}

export function jsx(type, props, key) {
  return makeNode(type, props, key);
}

export const jsxs = jsx;

export function h(type, props, ...children) {
  if (children.length === 0) {
    return makeNode(type, props, undefined);
  }
  const next = Object.assign({}, props);
  next.children = children.length === 1 ? children[0] : children;
  return makeNode(type, next, undefined);
}

export const createElement = h;

// Fragment is a host-level tag: the bridge splices its children in place.
export const Fragment = "#fragment";

// Host elements are plain tag descriptors. They are strings so TypeScript can
// expose precise prop types while the runtime hands a tag straight to C++.
export const Column = "Column";
export const Row = "Row";
export const Flow = "Flow";
export const Stack = "Stack";
export const Theme = "Theme";
export const Text = "Text";
export const Button = "Button";
export const IconButton = "IconButton";
export const Image = "Image";
export const Select = "Select";
export const ComboBox = "ComboBox";
export const DatePicker = "DatePicker";
export const TimePicker = "TimePicker";
export const RefreshBox = "RefreshBox";
export const Pager = "Pager";
export const SplitLayout = "SplitLayout";
export const TopAppBar = "TopAppBar";
export const Divider = "Divider";
export const Spacer = "Spacer";
export const ScrollView = "ScrollView";
export const Checkbox = "Checkbox";
export const RadioButton = "RadioButton";
export const Switch = "Switch";
export const Slider = "Slider";
export const ProgressBar = "ProgressBar";
export const ProgressCircle = "ProgressCircle";
export const TextField = "TextField";
export const Chip = "Chip";
export const IndexedPages = "IndexedPages";
export const SelectionArea = "SelectionArea";
export const VirtualList = "VirtualList";
export const VirtualGrid = "VirtualGrid";
export const Tabs = "Tabs";
export const SegmentedButton = "SegmentedButton";
export const NavigationPane = "NavigationPane";
export const NavigationBar = "NavigationBar";

export function mount(root) {
  globalThis.__huxerui_root = typeof root === "function" ? jsx(root, EMPTY_PROPS) : root;
}

function makeSetter(handle) {
  return function setState(next) {
    const value =
      typeof next === "function" ? next(__huxerui_stateValue(handle)) : next;
    __huxerui_setState(handle, value);
  };
}

export function useState(initial) {
  const handle = __huxerui_useState(initial);
  return [__huxerui_stateValue(handle), makeSetter(handle)];
}

export function useReducer(reducer, initialArg, init) {
  const initial = init === undefined ? initialArg : init(initialArg);
  const [state, setState] = useState(initial);
  return [
    state,
    function dispatch(action) {
      setState((current) => reducer(current, action));
    },
  ];
}

export function useRef(initial) {
  return __huxerui_useRef(initial);
}

function depsEqual(left, right) {
  if (left === null || right === null) {
    return false;
  }
  if (left.length !== right.length) {
    return false;
  }
  for (let index = 0; index < left.length; index += 1) {
    if (!Object.is(left[index], right[index])) {
      return false;
    }
  }
  return true;
}

export function useMemo(factory, deps) {
  const holder = __huxerui_useRef({ ready: false, deps: null, value: undefined });
  const record = holder.current;
  const nextDeps = deps === undefined ? null : deps;
  if (!record.ready || !depsEqual(record.deps, nextDeps)) {
    record.value = factory();
    record.deps = nextDeps;
    record.ready = true;
  }
  return record.value;
}

export function useCallback(callback, deps) {
  return useMemo(() => callback, deps);
}

export function useFocusRef() {
  return useMemo(() => {
    const id = __huxerui_useFocusRequester();
    return {
      id,
      requestFocus: () => __huxerui_requestFocus(id),
    };
  }, []);
}

export function usePopup() {
  return useMemo(() => {
    const id = __huxerui_usePopup();
    return {
      id,
      show: (render, options = {}) => __huxerui_showPopup(id, render, options),
      dismiss: (layer) => __huxerui_dismissPopup(id, layer),
    };
  }, []);
}

export function useBottomSheet() {
  return useMemo(() => {
    const id = __huxerui_useBottomSheet();
    return {
      id,
      show: (render, options = {}) => __huxerui_showBottomSheet(id, render, options),
      dismiss: (layer) => __huxerui_dismissBottomSheet(id, layer),
    };
  }, []);
}

export function useSnackBar() {
  return useMemo(() => {
    const id = __huxerui_useSnackBar();
    return {
      id,
      show: (message, duration = 4) =>
        __huxerui_showSnackBar(id, message, "", undefined, duration),
      showAction: (message, action, onAction, duration = 4) =>
        __huxerui_showSnackBar(id, message, action, onAction, duration),
      dismiss: (layer) => __huxerui_dismissSnackBar(id, layer),
    };
  }, []);
}

export function useMenu() {
  return useMemo(() => {
    const id = __huxerui_useMenu();
    return {
      id,
      show: (items) => __huxerui_showMenuAnchored(id, items),
      showAt: (x, y, items) => __huxerui_showMenu(id, x, y, items),
      dismiss: (layer) => __huxerui_dismissMenu(id, layer),
    };
  }, []);
}

export function useDialog() {
  return useMemo(() => {
    const id = __huxerui_useDialog();
    return {
      id,
      show: (options = {}) => __huxerui_showDialog(id, options),
      showContent: (render, options = {}) =>
        __huxerui_showDialogContent(id, render, options),
      dismiss: (layer) => __huxerui_dismissDialog(id, layer),
    };
  }, []);
}

export function useToast() {
  return useMemo(() => {
    const id = __huxerui_useToast();
    return {
      id,
      show: (message, duration = 2) => __huxerui_showToast(id, message, duration),
      dismiss: (layer) => __huxerui_dismissToast(id, layer),
    };
  }, []);
}

export function useScrollRef(initialOffset = 0) {
  return useMemo(() => {
    const id = __huxerui_useScrollController(initialOffset);
    return {
      id,
      scrollTo: (offset) => __huxerui_scrollTo(id, offset),
      scrollBy: (delta) => __huxerui_scrollBy(id, delta),
      scrollToItem: (index, alignment = "start") =>
        __huxerui_scrollToItem(id, index, alignment),
      offset: () => __huxerui_scrollOffset(id),
      metrics: () => __huxerui_scrollMetrics(id),
    };
  }, []);
}

export function useNavigation(initialPage) {
  const [pages, setPages] = useState(() => (initialPage ? [initialPage] : []));
  const pagesRef = useRef(pages);
  pagesRef.current = pages;
  return useMemo(() => ({
    get depth() {
      return pagesRef.current.length;
    },
    get canPop() {
      return pagesRef.current.length > 1;
    },
    entries() {
      return pagesRef.current;
    },
    current() {
      return pagesRef.current.length > 0
        ? pagesRef.current[pagesRef.current.length - 1]
        : null;
    },
    push(page) {
      setPages((current) => [...current, page]);
    },
    pop() {
      if (pagesRef.current.length <= 1) {
        return false;
      }
      setPages((current) => (current.length > 1 ? current.slice(0, -1) : current));
      return true;
    },
    replace(page) {
      setPages((current) => (
        current.length > 0 ? [...current.slice(0, -1), page] : [page]
      ));
    },
    reset(page) {
      setPages(page ? [page] : []);
    },
  }), []);
}

function renderNavigationPage(page) {
  return typeof page === "function" ? h(page) : page;
}

export function NavigationStack({ navigation, ...props }) {
  const pages = navigation.entries();
  if (pages.length === 0) {
    return null;
  }
  const rendered = pages.map((page, index) => {
    const node = renderNavigationPage(page);
    if (node === null || node === undefined || typeof node !== "object" || Array.isArray(node)) {
      return node;
    }
    if (node.key !== undefined && node.key !== null) {
      return node;
    }
    return { ...node, key: "page-" + index };
  });
  if (rendered.length === 0) {
    return null;
  }
  return h(Column, { ...props, onBack: () => navigation.pop() },
    h(IndexedPages, { selected: rendered.length - 1 }, rendered)
  );
}

export function useEffect(effect, deps) {
  __huxerui_useEffect(effect, deps === undefined ? null : deps);
}

export function useLayoutEffect(effect, deps) {
  __huxerui_useEffect(effect, deps === undefined ? null : deps);
}

// Timer functions are installed as host globals. These exports let bundles use
// an explicit import without relying on ambient globals.
export function setTimeout(callback, delay = 0) {
  return globalThis.setTimeout(callback, delay);
}

export function clearTimeout(handle) {
  globalThis.clearTimeout(handle);
}

export function setInterval(callback, delay = 0) {
  return globalThis.setInterval(callback, delay);
}

export function clearInterval(handle) {
  globalThis.clearInterval(handle);
}

if (typeof globalThis.queueMicrotask === "undefined") {
  globalThis.queueMicrotask = (callback) => {
    Promise.resolve().then(callback);
  };
}

export function queueMicrotask(callback) {
  globalThis.queueMicrotask(callback);
}

export function readClipboard() {
  return __huxerui_clipboardRead();
}

export function writeClipboard(text) {
  return __huxerui_clipboardWrite(String(text));
}

export function clipboardAvailable() {
  return __huxerui_clipboardAvailable();
}

export const clipboard = Object.freeze({
  read: readClipboard,
  write: writeClipboard,
  available: clipboardAvailable,
});

export function quitApplication() {
  __huxerui_quitApplication();
}

export function applicationLifecycle() {
  return __huxerui_applicationLifecycle();
}

export function fetch(url, options = {}) {
  return new Promise((resolve, reject) => {
    __huxerui_fetch(url, options, resolve, reject);
  });
}

if (typeof globalThis.fetch === "undefined") {
  globalThis.fetch = fetch;
}

// Small console shim; the host forwards it to RuntimeOptions::on_log.
function formatLogArgument(value) {
  if (typeof value === "string") {
    return value;
  }
  if (typeof value === "undefined") {
    return "undefined";
  }
  try {
    return JSON.stringify(value);
  } catch {
    return String(value);
  }
}

if (typeof globalThis.console === "undefined") {
  const emit = (level) => (...args) => {
    __huxerui_log(level, args.map(formatLogArgument).join(" "));
  };
  globalThis.console = {
    debug: emit("debug"),
    log: emit("info"),
    info: emit("info"),
    warn: emit("warn"),
    error: emit("error"),
  };
}
