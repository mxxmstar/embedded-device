# Common and render modules

- `modules/common/include/common/`: process, queue and logger headers.
- `modules/common/process/`: platform implementations and tests.
- `modules/render/include/render/`: renderer interfaces and configuration.
- `modules/render/video/`: OpenGL and DMA-BUF implementations.
- `modules/render/test/`: renderer tests.
- `app/main.cpp`: application entry point.

Existing `common/...` and `render/...` include names remain valid.
`Common::Headers` exposes shared headers without linking the process implementation. `Common::Common` aliases `common_lib`.
`Render::Render` aliases `render_lib`, exposes renderer headers and links `Media::Core`; OpenGL, GLFW, GLAD and spdlog are implementation dependencies.
Root-level `include/` and `src/` have been removed.
