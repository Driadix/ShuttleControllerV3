#!/usr/bin/env python3
"""THROWAWAY PROTOTYPE: repository shape for the V3 normative package.

This is intentionally a small executable design probe, not production tooling.
It compares two layouts and validates representative traceability slices in
memory so the answer does not depend on a persistent database or registry.
"""

from collections import defaultdict, deque
import json


LAYOUTS = {
    "A": {
        "name": "type-first records",
        "tree": [
            "items/EVD/EVD-0001.yaml",
            "items/REQ/REQ-0001.yaml",
            "items/ARCH/ARCH-0001.yaml",
            "items/VER/VER-0001.yaml",
            "governance/gates/G1.yaml",
            "manifests/design-baseline.yaml",
            "views/generated/traceability.json",
        ],
        "scores": {
            "navigation": 2,
            "normative boundaries": 2,
            "machine validation": 3,
            "reviewability": 2,
            "traceability growth": 3,
        },
        "tradeoff": "Typed records are easy to find, but one logical item is scattered across type directories.",
    },
    "B": {
        "name": "concern-first logical items",
        "tree": [
            "items/capability-operation-contracts/item.yaml",
            "items/capability-operation-contracts/records/DEC-0001.yaml",
            "items/capability-operation-contracts/records/REQ-0001.yaml",
            "items/safety-health/item.yaml",
            "items/safety-health/records/HAZ-0001.yaml",
            "items/safety-health/records/REQ-0002.yaml",
            "items/software-architecture/item.yaml",
            "items/software-architecture/records/ARCH-0001.yaml",
            "items/verification-strategy/records/VER-0001.yaml",
            "governance/gates/G1/manifest.yaml",
            "governance/gates/G1/checklist.yaml",
            "governance/approvals/G1-approval.yaml",
            "manifests/design-baseline.yaml",
            "views/generated/traceability.json",
            "views/generated/coverage.json",
            "diagrams/src/system-context.mmd",
        ],
        "scores": {
            "navigation": 3,
            "normative boundaries": 3,
            "machine validation": 3,
            "reviewability": 3,
            "traceability growth": 3,
        },
        "tradeoff": "The approval and revision boundary is visible at the logical item, while typed records remain explicit inside it.",
    },
}


RECORDS = [
    {
        "id": "EVD-0001",
        "type": "EVD",
        "slice": "capability contract",
        "item": "capability-operation-contracts",
        "item_revision": "r1",
        "maturity": "Approved",
        "provenance": {"repo": "Driadix/ShuttleController", "commit": "v1-main", "path": "src/Cntrl_V2.ino"},
        "confidence": "medium",
    },
    {
        "id": "DEC-0001",
        "type": "DEC",
        "slice": "capability contract",
        "item": "capability-operation-contracts",
        "item_revision": "r1",
        "maturity": "Approved",
        "approval_locator": "github:issue/9#resolution",
        "rationale": "Keep the operation identity and lifecycle boundary from the approved V3 catalog.",
    },
    {
        "id": "REQ-0001",
        "type": "REQ",
        "slice": "capability contract",
        "item": "capability-operation-contracts",
        "item_revision": "r1",
        "maturity": "Approved",
        "applicability": ["all shuttle profiles"],
        "acceptance": "The accepted request creates one operation identity and exposes lifecycle transitions.",
    },
    {
        "id": "EVD-0002",
        "type": "EVD",
        "slice": "safety invariant",
        "item": "safety-health",
        "item_revision": "r1",
        "maturity": "Approved",
        "provenance": {"repo": "Driadix/ShuttleController", "commit": "v1-main", "path": "src/Cntrl_V2.ino#motor_Force_Stop"},
        "confidence": "high",
    },
    {
        "id": "HAZ-0001",
        "type": "HAZ",
        "slice": "safety invariant",
        "item": "safety-health",
        "item_revision": "r1",
        "maturity": "Approved",
        "operating_context": "motion",
    },
    {
        "id": "REQ-0002",
        "type": "REQ",
        "slice": "safety invariant",
        "item": "safety-health",
        "item_revision": "r1",
        "maturity": "Approved",
        "applicability": ["motion"],
        "acceptance": "Safety precedence reaches a bounded safe stop for the motion context.",
    },
    {
        "id": "ARCH-0001",
        "type": "ARCH",
        "slice": "architecture decision",
        "item": "software-architecture",
        "item_revision": "r1",
        "maturity": "Approved",
        "allocation": "cooperative scheduler with bounded steps",
    },
    {
        "id": "VER-0001",
        "type": "VER",
        "slice": "verification obligation",
        "item": "verification-strategy",
        "item_revision": "r1",
        "maturity": "Approved",
        "method": "host property test plus target proving slice",
        "oracle": "safe-stop deadline and no motion after stop",
        "environment": "native and STM32 bench",
    },
    {
        "id": "VER-0002",
        "type": "VER",
        "slice": "safety verification obligation",
        "item": "verification-strategy",
        "item_revision": "r1",
        "maturity": "Approved",
        "method": "fault injection plus target proving slice",
        "oracle": "safe-stop deadline and no motion after stop",
        "environment": "STM32 bench with controlled link loss",
    },
    {
        "id": "WORK-0001",
        "type": "WORK",
        "slice": "implementation coverage",
        "item": "implementation-plan",
        "item_revision": "r1",
        "maturity": "Approved",
        "tracker_locator": "github:issue/next-v3-task",
    },
]


RELATIONS = [
    {"from": "DEC-0001", "relation": "supported-by", "to": "EVD-0001", "owner_item": "capability-operation-contracts"},
    {"from": "REQ-0001", "relation": "derived-from", "to": "DEC-0001", "owner_item": "capability-operation-contracts"},
    {"from": "HAZ-0001", "relation": "supported-by", "to": "EVD-0002", "owner_item": "safety-health"},
    {"from": "REQ-0002", "relation": "mitigates", "to": "HAZ-0001", "owner_item": "safety-health"},
    {"from": "REQ-0001", "relation": "allocated-to", "to": "ARCH-0001", "owner_item": "capability-operation-contracts"},
    {"from": "ARCH-0001", "relation": "satisfies", "to": "REQ-0001", "owner_item": "software-architecture"},
    {"from": "REQ-0002", "relation": "verified-by", "to": "VER-0002", "owner_item": "verification-strategy"},
    {"from": "REQ-0001", "relation": "verified-by", "to": "VER-0001", "owner_item": "verification-strategy"},
    {"from": "ARCH-0001", "relation": "verified-by", "to": "VER-0001", "owner_item": "verification-strategy"},
    {"from": "WORK-0001", "relation": "implements", "to": "ARCH-0001", "owner_item": "implementation-plan"},
]


ALLOWED_RELATIONS = {
    ("DEC", "supported-by", "EVD"),
    ("REQ", "derived-from", "DEC"),
    ("HAZ", "supported-by", "EVD"),
    ("REQ", "mitigates", "HAZ"),
    ("REQ", "allocated-to", "ARCH"),
    ("ARCH", "satisfies", "REQ"),
    ("REQ", "verified-by", "VER"),
    ("ARCH", "verified-by", "VER"),
    ("WORK", "implements", "REQ"),
    ("WORK", "implements", "ARCH"),
}

ACYCLIC_RELATIONS = {"derived-from", "refines", "supersedes", "depends-on"}
RELATION_OWNER = {
    "supported-by": "from",
    "derived-from": "from",
    "mitigates": "from",
    "allocated-to": "from",
    "satisfies": "from",
    "verified-by": "to",
    "implements": "from",
}


MANIFEST = {
    "manifest_id": "DESIGN-BASELINE-0001",
    "baseline": "Design Baseline",
    "item_revisions": [
        {"item": "capability-operation-contracts", "revision": "r1", "gate": "G1"},
        {"item": "safety-health", "revision": "r1", "gate": "G2"},
        {"item": "software-architecture", "revision": "r1", "gate": "G4"},
        {"item": "verification-strategy", "revision": "r1", "gate": "G5"},
        {"item": "implementation-plan", "revision": "r1", "gate": "G6"},
    ],
    "gates": [
        {
            "id": "G1",
            "checklist_version": "g1-v1",
            "scope": ["capability-operation-contracts"],
            "approval": {"status": "Approved", "issue": "github:issue/9#resolution", "commit": "prototype-input"},
        },
        {
            "id": "G2",
            "checklist_version": "g2-v1",
            "scope": ["safety-health"],
            "approval": {"status": "Approved", "issue": "github:issue/safety", "commit": "prototype-input"},
        },
        {
            "id": "G4",
            "checklist_version": "g4-v1",
            "scope": ["software-architecture"],
            "approval": {"status": "Approved", "issue": "github:issue/10#resolution", "commit": "prototype-input"},
        },
        {
            "id": "G5",
            "checklist_version": "g5-v1",
            "scope": ["verification-strategy"],
            "approval": {"status": "Approved", "issue": "github:issue/8#resolution", "commit": "prototype-input"},
        },
        {
            "id": "G6",
            "checklist_version": "g6-v1",
            "scope": ["implementation-plan"],
            "approval": {"status": "Approved", "issue": "github:issue/1#resolution", "commit": "prototype-input"},
        },
    ],
}


def record_map():
    return {record["id"]: record for record in RECORDS}


def validate_ids(records):
    seen = set()
    for record in records:
        identifier = record["id"]
        if identifier in seen:
            raise ValueError(f"duplicate canonical id: {identifier}")
        if identifier in {"REQ-0000", "ARCH-0000"}:
            raise ValueError(f"reserved id reused: {identifier}")
        prefix, number = identifier.split("-")
        if prefix != record["type"] or not number.isdigit():
            raise ValueError(f"typed id mismatch: {identifier}")
        seen.add(identifier)
    return f"{len(seen)} unique typed IDs; no collision or reuse detected"


def validate_relations(records, relations):
    records_by_id = record_map()
    seen = set()
    outgoing = defaultdict(list)
    for edge in relations:
        key = (edge["from"], edge["relation"], edge["to"])
        if key in seen:
            raise ValueError(f"duplicate canonical relation: {key}")
        seen.add(key)
        source = records_by_id.get(edge["from"])
        target = records_by_id.get(edge["to"])
        if source is None or target is None:
            raise ValueError(f"relation points to unknown record: {key}")
        shape = (source["type"], edge["relation"], target["type"])
        if shape not in ALLOWED_RELATIONS:
            raise ValueError(f"relation type pair is not allowed: {shape}")
        owner_record = source if RELATION_OWNER[edge["relation"]] == "from" else target
        if edge.get("owner_item") != owner_record["item"]:
            raise ValueError(f"relation owner does not match schema rule: {key}")
        if edge["relation"] in ACYCLIC_RELATIONS:
            outgoing[edge["from"]].append(edge["to"])

    visiting = set()
    visited = set()

    def visit(identifier):
        if identifier in visiting:
            raise ValueError(f"relation cycle detected at {identifier}")
        if identifier in visited:
            return
        visiting.add(identifier)
        for child in outgoing[identifier]:
            visit(child)
        visiting.remove(identifier)
        visited.add(identifier)

    for identifier in records_by_id:
        visit(identifier)
    return f"{len(seen)} canonical relations; type pairs, ownership shape and acyclicity pass"


def validate_manifest(records, manifest):
    declared = {(entry["item"], entry["revision"]) for entry in manifest["item_revisions"]}
    missing = [record["id"] for record in records if (record["item"], record["item_revision"]) not in declared]
    if missing:
        raise ValueError(f"manifest cannot reconstruct records: {missing}")
    for gate in manifest["gates"]:
        approval = gate["approval"]
        if approval["status"] not in {"Approved", "Rejected"}:
            raise ValueError(f"invalid gate outcome: {gate['id']}")
        if not approval["issue"] or not approval["commit"] or not gate["checklist_version"]:
            raise ValueError(f"incomplete approval record: {gate['id']}")
    return f"{len(declared)} item revisions and {len(manifest['gates'])} gate approvals reconstruct the baseline"


def coverage_diagnostics(records, relations):
    by_id = record_map()
    incoming = defaultdict(list)
    outgoing = defaultdict(list)
    for edge in relations:
        outgoing[edge["from"]].append(edge)
        incoming[edge["to"]].append(edge)

    diagnostics = []
    for record in records:
        identifier = record["id"]
        kind = record["type"]
        if kind == "EVD" and not record.get("provenance"):
            diagnostics.append(f"{identifier}: missing immutable provenance")
        if kind == "DEC" and not any(edge["relation"] == "supported-by" for edge in outgoing[identifier]):
            diagnostics.append(f"{identifier}: no supporting evidence")
        if kind == "REQ" and not record.get("acceptance"):
            diagnostics.append(f"{identifier}: missing acceptance semantics")
        if kind in {"REQ", "ARCH"} and not any(edge["relation"] == "verified-by" for edge in outgoing[identifier]):
            diagnostics.append(f"{identifier}: no planned verification")
        if kind == "VER" and not incoming[identifier]:
            diagnostics.append(f"{identifier}: orphan verification specification")
        if kind == "WORK" and not any(edge["relation"] == "implements" for edge in outgoing[identifier]):
            diagnostics.append(f"{identifier}: no implementation target")
    return diagnostics


def generated_views(records, relations):
    outgoing = defaultdict(list)
    incoming = defaultdict(list)
    for edge in relations:
        outgoing[edge["from"]].append({"relation": edge["relation"], "to": edge["to"]})
        incoming[edge["to"]].append({"relation": edge["relation"], "from": edge["from"]})
    traceability = []
    for record in sorted(records, key=lambda value: value["id"]):
        traceability.append(
            {
                "id": record["id"],
                "type": record["type"],
                "item_revision": f"{record['item']}@{record['item_revision']}",
                "outgoing": sorted(outgoing[record["id"]], key=lambda value: (value["relation"], value["to"])),
                "incoming": sorted(incoming[record["id"]], key=lambda value: (value["relation"], value["from"])),
            }
        )
    return {
        "traceability": traceability,
        "status": [
            {
                "id": record["id"],
                "maturity": record["maturity"],
                "baseline_membership": "Design Baseline",
                "trace_health": "Complete",
            }
            for record in sorted(records, key=lambda value: value["id"])
        ],
        "coverage": coverage_diagnostics(records, relations),
    }


def impact_closure(changed_id, relations):
    graph = defaultdict(set)
    for edge in relations:
        graph[edge["from"]].add(edge["to"])
        graph[edge["to"]].add(edge["from"])
    found = {changed_id}
    queue = deque([changed_id])
    while queue:
        current = queue.popleft()
        for neighbor in graph[current]:
            if neighbor not in found:
                found.add(neighbor)
                queue.append(neighbor)
    return sorted(found)


def show_layouts():
    print("LAYOUT CANDIDATES")
    for key, layout in LAYOUTS.items():
        score = sum(layout["scores"].values())
        print(f"\n[{key}] {layout['name']} - {score}/15")
        for path in layout["tree"]:
            print(f"  {path}")
        print(f"  tradeoff: {layout['tradeoff']}")
    print("\nVERDICT: choose B. Logical item directories expose revision and approval boundaries; typed records, manifests and generated views remain machine-addressable.")


def show_schema():
    print("\nITEM METADATA SHAPE")
    print(json.dumps({
        "item_id": "capability-operation-contracts",
        "class": "Normative",
        "owner": "firmware-architecture",
        "revision": {"id": "r1", "content_digest": "sha256:<immutable>"},
        "lifecycle": "Approved",
        "records": ["REQ-0001", "DEC-0001"],
        "gate": "G1",
    }, indent=2, ensure_ascii=False))


def main():
    show_layouts()
    show_schema()
    print("\nVALIDATION")
    print(f"- IDs: {validate_ids(RECORDS)}")
    print(f"- Relations: {validate_relations(RECORDS, RELATIONS)}")
    print(f"- Manifest: {validate_manifest(RECORDS, MANIFEST)}")
    views = generated_views(RECORDS, RELATIONS)
    print(f"- Generated views: {len(views['traceability'])} trace rows, {len(views['status'])} status rows, reverse links included")
    print(f"- Coverage diagnostics: {len(views['coverage'])} (expected 0)")
    if views["coverage"]:
        for diagnostic in views["coverage"]:
            print(f"  {diagnostic}")
    print("\nREPRESENTATIVE SLICES")
    for slice_name in sorted({record["slice"] for record in RECORDS}):
        ids = [record["id"] for record in RECORDS if record["slice"] == slice_name]
        print(f"- {slice_name}: {', '.join(ids)}")
    print("\nSEMANTIC CHANGE")
    impacted = impact_closure("REQ-0001", RELATIONS)
    print("- Change REQ-0001 acceptance semantics: candidate impact closure = " + ", ".join(impacted))
    print("- Reviewer dispositions required: REQ-0001, ARCH-0001, VER-0001, WORK-0001 and their G1/G4/G5/G6 approvals.")
    print("- Reverse links and coverage are generated views; no second canonical relation store is needed.")


if __name__ == "__main__":
    main()
