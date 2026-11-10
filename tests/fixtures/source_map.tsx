import { mount, h, Column, Text } from "@huxerui/tsx";

function Leaf() {
  throw new Error("mapped boom");
}

function App() {
  return h(Column, null,
    h(Leaf),
    h(Text, null, "unreachable")
  );
}

mount(h(App));
