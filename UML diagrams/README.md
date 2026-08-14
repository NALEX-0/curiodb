# CurioDB UML diagrams

This directory contains PlantUML source files documenting CurioDB from several
perspectives:

- [`select-sequence.puml`](select-sequence.puml) — sequence diagram for parsing,
  planning, and executing an indexed `SELECT` query.
- [`statement-activity.puml`](statement-activity.puml) — activity diagram for
  processing a statement from CLI input to result output.

The `.puml` files are text-based and should be kept in sync with architectural
changes. Render them with a PlantUML editor extension or with the PlantUML CLI:

```sh
plantuml "UML diagrams"/*.puml
```

Rendering creates image files beside the sources. Generated images are optional;
the PlantUML files are the maintained documentation.
