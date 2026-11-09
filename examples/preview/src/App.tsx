import { mount, Button, Column, Divider, Row, Text, useState } from "@huxerui/tsx";

function Stat(props: { label: string; value: string; accent?: string }) {
  return (
    <Column padding={16} spacing={6} cornerRadius={12} background="#f4f6f8" grow={1}>
      <Text role="label" foreground="#5b6573">{props.label}</Text>
      <Text role="title" color={props.accent ?? "#1f2328"}>{props.value}</Text>
    </Column>
  );
}

function App() {
  const [tab, setTab] = useState("Overview");

  return (
    <Row key="preview-root" padding={24} spacing={24} crossAlign="stretch">
      <Column width={220} spacing={10}>
        <Text role="title">HuxerUI TSX</Text>
        <Text foreground="#5b6573">Layout preview</Text>
        <Divider />
        <Button onClick={() => setTab("Overview")}>Overview</Button>
        <Button onClick={() => setTab("Activity")}>Activity</Button>
        <Button onClick={() => setTab("Settings")}>Settings</Button>
        <Button onClick={() => setTab("hoot")}>hoot</Button>
        <Button onClick={() => setTab("three")}>three</Button>
           <Button onClick={() => setTab("wwww")}>www</Button>
        <Divider />
        <Text role="label" foreground="#5b6573">
          Edit App.tsx and save; the window reloads the layout with fresh state.
        </Text>
      </Column>

      <Column spacing={16} grow={1}>
        <Text role="title">{tab}</Text>
        <Row spacing={12} mainAlign="spaceBetween">
          <Stat label="Sessions" value="1,248" />
          <Stat label="Errors" value="12" accent="#cf222e" />
          <Stat label="Latency" value="42 ms" accent="#1a7f37" />
        </Row>
        <Column padding={20} spacing={12} background="#ffffff" border={{ color: "#d8dee4", width: 1 }} cornerRadius={16}>
          <Text role="label" foreground="#5b6573">Selected section</Text>
          <Text role="title">{tab}</Text>
          <Text>
            This page is described entirely in TSX. Every component below is a native HuxerUI
            View, so layout, text shaping, input, and accessibility use the same engine as a
            C++ application.
          </Text>
          <Row spacing={8}>
            <Button key="primary-action" onClick={() => setTab(`${tab} ✓`)}>Apply</Button>
            <Button onClick={() => setTab("Overview")}>Reset</Button>
          </Row>
        </Column>
      </Column>
    </Row>
  );
}

mount(<App />);
