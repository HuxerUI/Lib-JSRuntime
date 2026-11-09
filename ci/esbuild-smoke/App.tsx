import { mount, Button, Column, Text, useState } from "@huxerui/tsx";

function App() {
  const [count, setCount] = useState(0);

  return (
    <Column padding={24} spacing={12} crossAlign="start">
      <Text role="title">Count: {count}</Text>
      <Button key="increment" onClick={() => setCount(count + 1)}>Increment</Button>
    </Column>
  );
}

mount(<App />);
