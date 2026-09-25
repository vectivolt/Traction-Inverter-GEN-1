// circuit-net.mjs — the golden per-board netlist, read from dist/boards/<board>/circuit.json (source_component /
// source_port / source_net / source_trace). Shared by erc-audit.mjs and kicad-sch-verify.mjs so both judge the
// same connectivity. Every port gets `ref` (component name) and `net`: the source_net name, or "@<root port id>"
// for a port on no named net (an anonymous junction or an unconnected pin).
import { readFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";

const ROOT = join(dirname(fileURLToPath(import.meta.url)), "..");

export function loadCircuit(board) {
  const j = JSON.parse(readFileSync(join(ROOT, "dist", "boards", board, "circuit.json"), "utf8"));
  const comps = j.filter((e) => e.type === "source_component");
  const ports = j.filter((e) => e.type === "source_port");
  const nets = new Map(j.filter((e) => e.type === "source_net").map((n) => [n.source_net_id, n.name]));
  const traces = j.filter((e) => e.type === "source_trace");
  const parent = new Map();
  const find = (x) => { while (parent.get(x) !== x) { parent.set(x, parent.get(parent.get(x))); x = parent.get(x); } return x; };
  const uni = (a, b) => { a = find(a); b = find(b); if (a !== b) parent.set(a, b); };
  for (const p of ports) parent.set(p.source_port_id, p.source_port_id);
  const groupNet = new Map();
  for (const t of traces) {
    const ps = t.connected_source_port_ids ?? [];
    for (let i = 1; i < ps.length; i++) uni(ps[0], ps[i]);
    for (const nid of t.connected_source_net_ids ?? []) if (ps.length) groupNet.set(find(ps[0]), nets.get(nid));
  }
  for (const [g, n] of [...groupNet]) groupNet.set(find(g), n);
  const compName = new Map(comps.map((c) => [c.source_component_id, c.name]));
  for (const p of ports) {
    const r = find(p.source_port_id);
    p.ref = compName.get(p.source_component_id);
    p.net = groupNet.get(r) ?? `@${r}`;
  }
  return { comps, compName, ports };
}
