import {
  mount,
  Button,
  Chip,
  Column,
  Divider,
  NavigationPane,
  ProgressBar,
  Row,
  ScrollView,
  Slider,
  Switch,
  Tabs,
  Text,
  TextField,
  type Child,
  VirtualGrid,
  VirtualList,
  useBottomSheet,
  useDialog,
  useFocusRef,
  useMenu,
  usePopup,
  useScrollRef,
  useSnackBar,
  useToast,
  useState,
} from "@huxerui/tsx";

type StatProps = {
  label: string;
  value: string;
  color?: string;
};

function Stat(props: StatProps) {
  return (
    <Column padding={16} spacing={6} cornerRadius={12} background="#f4f6f8" grow={1}>
      <Text role="label" foreground="#5b6573">{props.label}</Text>
      <Text role="title" color={props.color ?? "#1f2328"}>{props.value}</Text>
    </Column>
  );
}

type SectionProps = {
  title: string;
  description: string;
  children?: Child;
};

function Section(props: SectionProps) {
  return (
    <Column
      padding={18}
      spacing={12}
      background="#ffffff"
      border={{ color: "#d8dee4", width: 1 }}
      cornerRadius={14}
    >
      <Text role="title">{props.title}</Text>
      <Text foreground="#5b6573">{props.description}</Text>
      {props.children}
    </Column>
  );
}

const rows = Array.from({ length: 100 }, (_, index) => `Row ${index + 1}`);
const tiles = Array.from({ length: 60 }, (_, index) => `Tile ${index + 1}`);

function Overview() {
  const [tab, setTab] = useState(0);

  return (
    <Column spacing={16}>
      <Tabs
        labels={["Overview", "Activity", "Settings"]}
        selected={tab}
        onChange={setTab}
      />
      <Row spacing={12} mainAlign="spaceBetween">
        <Stat label="Sessions" value="1,248" />
        <Stat label="Errors" value="12" color="#cf222e" />
        <Stat label="Latency" value="42 ms" color="#1a7f37" />
      </Row>
      <Section
        title="Native by construction"
        description="Every element below comes from the HuxerUI TSX bridge."
      >
        <Row spacing={8}>
          <Button tooltip="Show a toast">Toast</Button>
          <Button tooltip="Show a dialog">Dialog</Button>
          <Button tooltip="Show a menu">Menu</Button>
          <Button tooltip="Show a popup">Popup</Button>
        </Row>
      </Section>
    </Column>
  );
}

type VirtualProps = {
  scroll: ReturnType<typeof useScrollRef>;
};

function VirtualPage(props: VirtualProps) {
  return (
    <Column spacing={16}>
      <Section
        title="VirtualList"
        description="Fixed extents, 100 logical rows, native scrolling."
      >
        <Row spacing={8}>
          <Button onClick={() => props.scroll.scrollTo(240)}>Scroll list</Button>
          <Button onClick={() => props.scroll.scrollTo(0)}>Top</Button>
        </Row>
        <VirtualList
          key="virtual-list"
          items={rows}
          itemExtent={36}
          scrollRef={props.scroll}
          frame={{ height: 220 }}
          renderItem={(item, index) => (
            <Text key={index}>{item}</Text>
          )}
        />
      </Section>

      <Section
        title="VirtualGrid"
        description="Two-column grid with a fixed row extent."
      >
        <VirtualGrid
          key="virtual-grid"
          items={tiles}
          columns={2}
          rowExtent={44}
          rowSpacing={6}
          columnSpacing={6}
          frame={{ height: 180 }}
          renderItem={(item, index) => (
            <Text key={index}>{item}</Text>
          )}
        />
      </Section>
    </Column>
  );
}

type FormProps = {
  focus: ReturnType<typeof useFocusRef>;
};

function FormsPage(props: FormProps) {
  const [text, setText] = useState("HuxerUI");
  const [edit, setEdit] = useState("Edit the field to inspect onEdit");
  const [enabled, setEnabled] = useState(true);
  const [progress, setProgress] = useState(0.65);
  const [choice, setChoice] = useState(1);

  return (
    <Column spacing={16}>
      <Section
        title="Controlled TextField"
        description="Selection, validation, focus ref, and complete onEdit payload."
      >
        <TextField
          key="showcase-field"
          ref={props.focus}
          value={text}
          label="Project name"
          placeholder="Type here"
          selection={{ anchor: text.length, active: text.length }}
          validation={{
            status: text.length === 0 ? "invalid" : "valid",
            message: "Project name is required",
          }}
          onChange={setText}
          onEdit={(event) => setEdit(`onEdit: ${event.text}`)}
          onSubmit={() => setEdit("submitted")}
        />
        <Row spacing={8}>
          <Button onClick={() => props.focus.requestFocus()}>Focus field</Button>
          <Text role="label" foreground="#5b6573">{edit}</Text>
        </Row>
      </Section>

      <Section title="Controls" description="Switch, slider, chip, and progress.">
        <Row spacing={12} crossAlign="center">
          <Switch label="Enabled" checked={enabled} onChange={setEnabled} />
          <Chip label={choice === 1 ? "Selected" : "Chip"} selected={choice === 1} onChange={() => setChoice(1)} />
          <Text role="label" foreground="#5b6573">
            {enabled ? "Switch on" : "Switch off"}
          </Text>
        </Row>
        <Slider value={progress} min={0} max={1} step={0.05} onChange={setProgress} />
        <ProgressBar progress={progress} />
      </Section>
    </Column>
  );
}

function DialogsPage() {
  const toast = useToast();
  const dialog = useDialog();
  const menu = useMenu();
  const popup = usePopup();
  const snack = useSnackBar();
  const sheet = useBottomSheet();
  const [status, setStatus] = useState("No presentation shown");

  return (
    <Column spacing={16}>
      <Section
        title="Presentation APIs"
        description="Toast, Dialog, Menu, Popup, SnackBar, and BottomSheet are window-scoped."
      >
        <Row spacing={8}>
          <Button
            onClick={() => {
              toast.show("Toast from TSX", 2);
              setStatus("toast.show");
            }}
          >
            Toast
          </Button>
          <Button
            onClick={() => {
              dialog.show({
                title: "Delete project?",
                message: "This is a standard native dialog.",
                positive: "Delete",
                negative: "Cancel",
                onPositive: () => setStatus("dialog.positive"),
                onNegative: () => setStatus("dialog.negative"),
              });
              setStatus("dialog.show");
            }}
          >
            Dialog
          </Button>
          <Button
            menuAnchor={menu}
            onClick={() => {
              menu.show([
                { label: "Rename", onSelect: () => setStatus("menu.rename") },
                { section: true },
                { label: "Delete", disabled: true },
              ]);
              setStatus("menu.show");
            }}
          >
            Menu
          </Button>
          <Button
            popupAnchor={popup}
            onClick={() => {
              popup.show(() => (
                <Column padding={16} spacing={6}>
                  <Text role="title">Anchored popup</Text>
                  <Text>Arbitrary TSX content.</Text>
                </Column>
              ));
              setStatus("popup.show");
            }}
          >
            Popup
          </Button>
        </Row>

        <Row spacing={8}>
          <Button
            onClick={() => {
              snack.showAction("Project deleted", "Undo", () => setStatus("snack.undo"), 4);
              setStatus("snack.show");
            }}
          >
            SnackBar
          </Button>
          <Button
            onClick={() => {
              sheet.show(() => (
                <Column padding={20} spacing={10}>
                  <Text role="title">Bottom sheet</Text>
                  <Text>Custom TSX content.</Text>
                </Column>
              ));
              setStatus("sheet.show");
            }}
          >
            BottomSheet
          </Button>
          <Text role="label" foreground="#5b6573">{status}</Text>
        </Row>
      </Section>

      <Section title="Gestures" description="Drag, long press, and multi tap are recognized natively.">
        <Column
          key="gesture-surface"
          padding={20}
          background="#eef4ff"
          cornerRadius={12}
          dragThreshold={4}
          onDragStart={() => setStatus("drag.start")}
          onDragEnd={() => setStatus("drag.end")}
          onLongPressStart={() => setStatus("longPress.start")}
          onMultiTap={(event) => setStatus(`multiTap:${event.count}`)}
        >
          <Text>Drag, hold, or double tap this surface</Text>
        </Column>
      </Section>
    </Column>
  );
}

function App() {
  const [page, setPage] = useState(0);
  const scroll = useScrollRef();
  const focus = useFocusRef();

  return (
    <Row key="showcase-root" padding={20} spacing={20} crossAlign="stretch">
      <Column width={220} spacing={12}>
        <Text role="title">HuxerUI TSX</Text>
        <Text foreground="#5b6573">Capability showcase</Text>
        <Divider />
        <NavigationPane
          labels={["Overview", "Virtual", "Forms", "Dialogs"]}
          expanded
          selected={page}
          onChange={setPage}
        />
        <Divider />
        <Text role="label" foreground="#5b6573">
          Edit examples/showcase/src/App.tsx and save; the preview reloads with fresh state.
        </Text>
      </Column>

      <ScrollView grow={1}>
        <Column spacing={16}>
          {page === 0 && <Overview />}
          {page === 1 && <VirtualPage scroll={scroll} />}
          {page === 2 && <FormsPage focus={focus} />}
          {page === 3 && <DialogsPage />}
        </Column>
      </ScrollView>
    </Row>
  );
}

mount(<App />);
