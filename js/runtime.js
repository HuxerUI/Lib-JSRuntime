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

export function useEffect(effect, deps) {
  __huxerui_useEffect(effect, deps === undefined ? null : deps);
}

export function useLayoutEffect(effect, deps) {
  __huxerui_useEffect(effect, deps === undefined ? null : deps);
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
