// Automatic JSX runtime declarations for `"jsx": "react-jsx"`.
// `jsxImportSource` resolves the implementation from "@huxerui/tsx".

import type { Child, VNode } from "./index";

export { Fragment } from "./index";

export function jsx(type: unknown, props: Record<string, unknown> | null, key?: unknown): VNode;
export const jsxs: typeof jsx;

export namespace JSX {
  type Element = Child;
  type ElementType = (props: never) => Child;

  interface ElementChildrenAttribute {
    children: {};
  }

  interface IntrinsicAttributes {
    key?: string | number;
  }
}
