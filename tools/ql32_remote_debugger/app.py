"""Small PySide6 front end for the QL/32 serial debugger."""

from __future__ import annotations

import json
import math
import os
import re
import tempfile
from pathlib import Path

from PySide6.QtCore import QRect, QSize, Qt, QTimer, QSignalBlocker, Signal
from PySide6.QtGui import QColor, QPainter, QTextFormat
from PySide6.QtWidgets import (
    QApplication, QCheckBox, QComboBox, QFileDialog, QHBoxLayout, QLabel,
    QInputDialog,
    QLineEdit, QMainWindow, QMessageBox, QPlainTextEdit, QProgressBar,
    QPushButton, QSpinBox, QSplitter, QTabWidget, QTreeWidget, QTreeWidgetItem,
    QTextEdit, QVBoxLayout, QWidget,
)

from .serial_worker import SerialWorker


class LineNumberArea(QWidget):
    def __init__(self, editor):
        super().__init__(editor)
        self.editor = editor

    def sizeHint(self):
        return QSize(self.editor.line_number_area_width(), 0)

    def paintEvent(self, event):
        self.editor.paint_line_numbers(event)

    def mousePressEvent(self, event):
        if event.button() == Qt.MouseButton.LeftButton:
            self.editor.toggle_breakpoint_at_y(event.position().y())


class SourceEditor(QPlainTextEdit):
    breakpoint_changed = Signal(int, bool)

    def __init__(self):
        super().__init__()
        self.breakpoints: set[int] = set()
        self.execution_line: int | None = None
        self.line_area = LineNumberArea(self)
        self.blockCountChanged.connect(self._update_margin)
        self.updateRequest.connect(self._update_area)
        self.cursorPositionChanged.connect(self.highlight_current_line)
        self._update_margin()
        self.highlight_current_line()

    def line_number_area_width(self):
        digits = max(2, len(str(max(1, self.blockCount()))))
        return 12 + self.fontMetrics().horizontalAdvance("9") * digits

    def _update_margin(self, *_):
        self.setViewportMargins(self.line_number_area_width(), 0, 0, 0)

    def _update_area(self, rect, dy):
        if dy:
            self.line_area.scroll(0, dy)
        else:
            self.line_area.update(0, rect.y(), self.line_area.width(), rect.height())
        if rect.contains(self.viewport().rect()):
            self._update_margin()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        rect = self.contentsRect()
        self.line_area.setGeometry(QRect(rect.left(), rect.top(),
                                         self.line_number_area_width(), rect.height()))

    def paint_line_numbers(self, event):
        painter = QPainter(self.line_area)
        painter.fillRect(event.rect(), QColor("#20242b"))
        block = self.firstVisibleBlock()
        number = block.blockNumber()
        top = int(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        bottom = top + int(self.blockBoundingRect(block).height())
        while block.isValid() and top <= event.rect().bottom():
            if block.isVisible() and bottom >= event.rect().top():
                if number + 1 in self.breakpoints:
                    painter.setBrush(QColor("#e05252"))
                    painter.setPen(Qt.PenStyle.NoPen)
                    painter.drawEllipse(3, top + 4, 8, 8)
                painter.setPen(QColor("#bac2ce"))
                painter.drawText(self.line_area.width() - 5 -
                                 self.fontMetrics().horizontalAdvance(str(number + 1)),
                                 top, str(number + 1))
            block = block.next()
            top = bottom
            bottom = top + int(self.blockBoundingRect(block).height())
            number += 1

    def toggle_breakpoint_at_y(self, y):
        block = self.firstVisibleBlock()
        top = int(self.blockBoundingGeometry(block).translated(self.contentOffset()).top())
        while block.isValid():
            bottom = top + int(self.blockBoundingRect(block).height())
            if top <= y < bottom:
                self.toggle_breakpoint(block.blockNumber() + 1)
                return
            block = block.next()
            top = bottom

    def toggle_breakpoint(self, line):
        enabled = line not in self.breakpoints
        if enabled:
            self.breakpoints.add(line)
        else:
            self.breakpoints.remove(line)
        self.line_area.update()
        self.breakpoint_changed.emit(line, enabled)

    def highlight_current_line(self):
        selections = []
        if self.textCursor().blockNumber() + 1 != self.execution_line:
            selection = QTextEdit.ExtraSelection()
            selection.format.setBackground(QColor("#dcecff"))
            selection.format.setForeground(QColor("#20242b"))
            selection.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
            selection.cursor = self.textCursor()
            selection.cursor.clearSelection()
            selections.append(selection)
        if self.execution_line is not None:
            block = self.document().findBlockByNumber(self.execution_line - 1)
            if block.isValid():
                selection = QTextEdit.ExtraSelection()
                selection.format.setBackground(QColor("#ffe082"))
                selection.format.setForeground(QColor("#20242b"))
                selection.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
                selection.cursor = self.textCursor()
                selection.cursor.setPosition(block.position())
                selection.cursor.clearSelection()
                selections.append(selection)
        self.setExtraSelections(selections)

    def set_execution_line(self, line):
        self.execution_line = int(line) if line and int(line) > 0 else None
        self.highlight_current_line()

    def goto_source_line(self, line):
        if line < 1:
            return
        cursor = self.textCursor()
        cursor.movePosition(cursor.MoveOperation.Start)
        for _ in range(line - 1):
            if not cursor.movePosition(cursor.MoveOperation.NextBlock):
                break
        self.setTextCursor(cursor)
        self.centerCursor()


class DebuggerWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("QL/32 Remote Debugger")
        self.resize(1180, 760)
        self.worker = SerialWorker()
        self.worker.connected.connect(self._on_connected)
        self.worker.connection_failed.connect(self._on_connection_failed)
        self.worker.disconnected.connect(self._on_disconnected)
        self.worker.log_message.connect(self._log)
        self.worker.repl_output.connect(self._append_repl_output)
        self.worker.request_finished.connect(self._on_response)
        self.worker.transfer_progress.connect(self._on_transfer_progress)
        self.worker.transfer_status.connect(self._on_transfer_status)
        self.worker.transfer_finished.connect(self._on_transfer_finished)
        self.pending: dict[int, tuple] = {}
        self.transfer_jobs: dict[str, tuple] = {}
        self.current_task: int | None = None
        self.task_info: dict[int, dict] = {}
        self.current_source_path: str | None = None
        self.storage_roots: list[str] = []
        self.remote_dir = ""
        self.screen_capture = False
        self._is_connected = False
        self._inspect_inflight = False
        self._connected_port: str | None = None

        root = QWidget()
        outer = QVBoxLayout(root)
        outer.addLayout(self._connection_bar())
        self.tabs = QTabWidget()
        self.tabs.addTab(self._build_debug_tab(), "Debugger")
        self.tabs.addTab(self._build_files_tab(), "Files")
        self.tabs.addTab(self._build_screen_tab(), "Screen")
        self.tabs.addTab(self._build_repl_tab(), "REPL")
        outer.addWidget(self.tabs)
        self.setCentralWidget(root)
        self._refresh_ports()

        self.poll_timer = QTimer(self)
        self.poll_timer.timeout.connect(self._poll_debug_state)
        self.poll_timer.start(1200)

    def _connection_bar(self):
        row = QHBoxLayout()
        self.port_box = QComboBox()
        self.port_box.setMinimumWidth(220)
        self.refresh_ports_button = QPushButton("Refresh ports")
        self.refresh_ports_button.clicked.connect(self._refresh_ports)
        self.connect_button = QPushButton("Connect")
        self.connect_button.clicked.connect(self._connect)
        self.status_label = QLabel("Disconnected")
        self.board_label = QLabel("")
        row.addWidget(QLabel("Serial port"))
        row.addWidget(self.port_box)
        row.addWidget(self.refresh_ports_button)
        row.addWidget(self.connect_button)
        row.addWidget(self.status_label, 1)
        row.addWidget(self.board_label)
        return row

    def _build_debug_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        bar = QHBoxLayout()
        self.task_box = QComboBox()
        self.task_refresh = QPushButton("Refresh tasks")
        self.task_refresh.clicked.connect(lambda: self._request("tasks.list", callback="tasks"))
        self.attach_button = QPushButton("Attach to task")
        self.attach_button.setToolTip(
            "Attach to an existing task. Select its source file in Files first.")
        self.attach_button.clicked.connect(self._attach_selected)
        self.start_button = QPushButton("Start file paused")
        self.start_button.setToolTip(
            "Start the selected BASIC file with the debugger attached and paused at its first statement.")
        self.start_button.clicked.connect(self._start_selected)
        bar.addWidget(QLabel("Task"))
        bar.addWidget(self.task_box, 1)
        bar.addWidget(self.task_refresh)
        bar.addWidget(self.attach_button)
        bar.addWidget(self.start_button)
        layout.addLayout(bar)

        viewport = QHBoxLayout()
        self.viewport_x = self._spinbox(-4096, 4096, 0, "Canvas screen X")
        self.viewport_y = self._spinbox(-4096, 4096, 0, "Canvas screen Y")
        self.viewport_width = self._spinbox(1, 4096, 240, "Canvas width")
        self.viewport_height = self._spinbox(1, 4096, 135, "Canvas height")
        for label, widget in (("X", self.viewport_x), ("Y", self.viewport_y),
                              ("W", self.viewport_width), ("H", self.viewport_height)):
            viewport.addWidget(QLabel(label))
            viewport.addWidget(widget)
        self.screen_width = self._spinbox(1, 4096, 240, "Physical display width for grid")
        self.screen_height = self._spinbox(1, 4096, 135, "Physical display height for grid")
        viewport.addWidget(QLabel("Display W×H"))
        viewport.addWidget(self.screen_width)
        viewport.addWidget(self.screen_height)
        self.viewport_apply = QPushButton("Set selected canvas")
        self.viewport_apply.clicked.connect(self._set_selected_viewport)
        self.grid_button = QPushButton("Tile tasks in grid")
        self.grid_button.clicked.connect(self._tile_tasks)
        viewport.addWidget(self.viewport_apply)
        viewport.addWidget(self.grid_button)
        layout.addLayout(viewport)
        self.task_box.currentIndexChanged.connect(self._load_selected_viewport)

        split = QSplitter()
        self.editor = SourceEditor()
        self.editor.setPlaceholderText("Select a BASIC source file from device storage")
        self.editor.breakpoint_changed.connect(self._breakpoint_changed)
        split.addWidget(self.editor)

        right = QWidget()
        right_layout = QVBoxLayout(right)
        buttons = QHBoxLayout()
        self.continue_button = QPushButton("Continue")
        self.step_button = QPushButton("Step")
        self.next_button = QPushButton("Next")
        self.pause_button = QPushButton("Pause")
        for button, command in ((self.continue_button, "continue"),
                                (self.step_button, "step"),
                                (self.next_button, "next")):
            button.clicked.connect(lambda _=False, mode=command: self._resume(mode))
            buttons.addWidget(button)
        self.pause_button.clicked.connect(self._pause)
        buttons.addWidget(self.pause_button)
        right_layout.addLayout(buttons)
        self.where_label = QLabel("No task attached")
        self.where_label.setWordWrap(True)
        right_layout.addWidget(self.where_label)
        self.variables = QPlainTextEdit()
        self.variables.setReadOnly(True)
        self.variables.setPlaceholderText("Locals and globals")
        right_layout.addWidget(self.variables, 2)
        self.stack_view = QPlainTextEdit()
        self.stack_view.setReadOnly(True)
        self.stack_view.setPlaceholderText("Call stack, watches, statistics")
        right_layout.addWidget(self.stack_view, 1)
        eval_row = QHBoxLayout()
        self.eval_input = QLineEdit()
        self.eval_input.setPlaceholderText("Safe EVAL expression")
        eval_button = QPushButton("Evaluate")
        eval_button.clicked.connect(self._evaluate)
        eval_row.addWidget(self.eval_input, 1)
        eval_row.addWidget(eval_button)
        right_layout.addLayout(eval_row)
        watch_row = QHBoxLayout()
        self.watch_input = QLineEdit()
        self.watch_input.setPlaceholderText("Watch expression")
        add_watch = QPushButton("Add watch")
        add_watch.clicked.connect(lambda: self._watch("add"))
        remove_watch = QPushButton("Remove")
        remove_watch.clicked.connect(lambda: self._watch("remove"))
        watch_row.addWidget(self.watch_input, 1)
        watch_row.addWidget(add_watch)
        watch_row.addWidget(remove_watch)
        right_layout.addLayout(watch_row)
        diagnostics = QHBoxLayout()
        self.diagnostics_check = QCheckBox("Collect diagnostics")
        self.diagnostics_check.toggled.connect(self._toggle_diagnostics)
        reset_stats = QPushButton("Reset stats")
        reset_stats.clicked.connect(self._reset_stats)
        diagnostics.addWidget(self.diagnostics_check)
        diagnostics.addWidget(reset_stats)
        right_layout.addLayout(diagnostics)
        split.addWidget(right)
        split.setStretchFactor(0, 3)
        split.setStretchFactor(1, 2)
        layout.addWidget(split, 1)
        self.log_view = QPlainTextEdit()
        self.log_view.setReadOnly(True)
        self.log_view.setMaximumHeight(90)
        layout.addWidget(self.log_view)
        return page

    def _build_files_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        path_row = QHBoxLayout()
        self.root_box = QComboBox()
        self.path_input = QLineEdit()
        self.path_input.setPlaceholderText("Device directory")
        refresh = QPushButton("List")
        refresh.clicked.connect(self._list_files)
        path_row.addWidget(self.root_box)
        path_row.addWidget(self.path_input, 1)
        path_row.addWidget(refresh)
        layout.addLayout(path_row)
        self.file_tree = QTreeWidget()
        self.file_tree.setHeaderLabels(["Name", "Size", "Type"])
        self.file_tree.itemDoubleClicked.connect(self._file_double_clicked)
        self.file_tree.itemSelectionChanged.connect(self._file_selected)
        layout.addWidget(self.file_tree, 1)
        controls = QHBoxLayout()
        upload = QPushButton("Upload PC file")
        upload.clicked.connect(self._upload_file)
        download = QPushButton("Download selected")
        download.clicked.connect(self._download_file)
        controls.addWidget(upload)
        controls.addWidget(download)
        self.transfer_progress = QProgressBar()
        self.transfer_progress.setRange(0, 1000)
        self.transfer_progress.setValue(0)
        controls.addWidget(self.transfer_progress, 1)
        cancel = QPushButton("Cancel transfer")
        cancel.clicked.connect(self.worker.cancel_transfer)
        controls.addWidget(cancel)
        layout.addLayout(controls)
        return page

    def _build_screen_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        controls = QHBoxLayout()
        capture = QPushButton("Capture screen")
        capture.clicked.connect(self._capture_screen)
        save = QPushButton("Save image")
        save.clicked.connect(self._save_screen)
        self.screen_status = QLabel("No capture yet")
        controls.addWidget(capture)
        controls.addWidget(save)
        controls.addWidget(self.screen_status, 1)
        layout.addLayout(controls)
        self.screen_view = QLabel("Screen capture appears here")
        self.screen_view.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.screen_view.setMinimumSize(500, 360)
        self.screen_view.setStyleSheet("background:#20242b;color:#d0d5dc")
        layout.addWidget(self.screen_view, 1)
        self._screen_path: Path | None = None
        return page

    def _build_repl_tab(self):
        page = QWidget()
        layout = QVBoxLayout(page)
        self.repl_view = QPlainTextEdit()
        self.repl_view.setReadOnly(True)
        self.repl_view.setPlaceholderText(
            "QL/32 serial console output will appear here. Type a BASIC or monitor command below.")
        layout.addWidget(self.repl_view, 1)
        command_row = QHBoxLayout()
        self.repl_input = QLineEdit()
        self.repl_input.setPlaceholderText("Type a QL/32 command and press Enter")
        self.repl_input.returnPressed.connect(self._send_repl_command)
        send = QPushButton("Send")
        send.clicked.connect(self._send_repl_command)
        clear = QPushButton("Clear output")
        clear.clicked.connect(self.repl_view.clear)
        command_row.addWidget(self.repl_input, 1)
        command_row.addWidget(send)
        command_row.addWidget(clear)
        layout.addLayout(command_row)
        return page

    def _refresh_ports(self):
        current = self.port_box.currentData()
        self.port_box.clear()
        try:
            from serial.tools import list_ports
            for port in list_ports.comports():
                # list_ports reports enumerated ports, but stale or permission-
                # blocked device nodes can still appear on Unix. Do not open a
                # port to probe it: opening an ESP32 USB-UART may reset the board.
                if os.name != "nt" and (
                    not os.path.exists(port.device) or
                    not os.access(port.device, os.R_OK | os.W_OK)
                ):
                    continue
                self.port_box.addItem(f"{port.device} — {port.description}", port.device)
            if self.port_box.count() == 0:
                self.port_box.addItem("No available serial ports")
        except ImportError:
            self.port_box.addItem("Install pyserial to list ports")
        index = self.port_box.findData(current)
        if index >= 0:
            self.port_box.setCurrentIndex(index)

    def _connect(self):
        if self.worker.isRunning() and self.status_label.text() != "Disconnected":
            self.worker.disconnect_port()
            return
        port = self.port_box.currentData()
        if not port:
            self._show_error("Select a serial port. Install pyserial if the list is empty.")
            return
        self.status_label.setText("Connecting…")
        self.worker.connect_port(port)

    def _on_connected(self, port, info):
        self._is_connected = True
        self._connected_port = port
        self.board_label.setText(info.get("board", ""))
        if not info.get("supported"):
            self.status_label.setText("Unsupported board")
            self._show_error(f"{info.get('board')} is not supported by this remote debugger.")
            return
        self.status_label.setText(f"Connected: {port}")
        self.screen_capture = bool(info.get("screen_capture"))
        self.storage_roots = info.get("storage_roots", [])
        self.root_box.clear()
        self.root_box.addItems(self.storage_roots)
        if self.storage_roots:
            self.remote_dir = self.storage_roots[0]
            self.path_input.setText(self.remote_dir)
            self._list_files()
        self._request("tasks.list", callback="tasks")

    def _on_disconnected(self):
        self._is_connected = False
        self._connected_port = None
        self.status_label.setText("Disconnected")
        self.board_label.clear()
        self.current_task = None

    def _on_connection_failed(self, message):
        self._is_connected = False
        self.status_label.setText("Connection failed")
        self.board_label.clear()
        self._log(message)

    def _request(self, operation, *args, callback=None):
        request_id = self.worker.request(operation, *args)
        self.pending[request_id] = callback
        return request_id

    def _on_response(self, request_id, ok, payload):
        callback = self.pending.pop(request_id, None)
        if not ok:
            self._log(payload.get("error", "Device request failed"))
            if callback in ("start", "attach"):
                self._show_error(payload.get("error", "Device request failed"))
            self._inspect_inflight = False
            return
        if callback == "tasks":
            self._populate_tasks(payload)
        elif callback == "files":
            self._populate_files(payload)
        elif callback == "start":
            self.current_task = int(payload["task_id"])
            self._log(f"Started task {self.current_task}; debugger attached and paused at the start.")
            self._request("tasks.list", callback="tasks")
        elif callback == "viewport":
            self._log(f"Task {payload['task_id']} canvas set to "
                      f"({payload['canvas_origin_x']}, {payload['canvas_origin_y']}) "
                      f"{payload['canvas_width']}×{payload['canvas_height']} "
                      f"offset ({payload.get('canvas_offset_x', 0)}, "
                      f"{payload.get('canvas_offset_y', 0)})")
            self._inspect()
        elif callback == "attach":
            self.current_task = int(payload["task_id"])
            self._log(f"Source loaded; attached to task {self.current_task}.")
            self._inspect()
        elif callback == "inspect":
            self._inspect_inflight = False
            self._show_snapshot(payload)
        elif callback == "eval":
            self._log("EVAL = " + repr(payload.get("value")))
        elif callback == "capture-unavailable":
            self._show_error("This device does not support screen capture.")

    def _populate_tasks(self, tasks):
        old = self.task_box.currentData()
        self.task_info = {int(task["id"]): task for task in tasks}
        blocker = QSignalBlocker(self.task_box)
        self.task_box.clear()
        for task in tasks:
            label = f"{task['id']} — {task['status']}"
            self.task_box.addItem(label, task["id"])
        index = self.task_box.findData(old)
        if index >= 0:
            self.task_box.setCurrentIndex(index)
        del blocker
        if old != self.task_box.currentData():
            self._load_selected_viewport()

    @staticmethod
    def _spinbox(minimum, maximum, value, tooltip):
        box = QSpinBox()
        box.setRange(minimum, maximum)
        box.setValue(value)
        box.setToolTip(tooltip)
        box.setMaximumWidth(90)
        return box

    def _load_selected_viewport(self, *_):
        task_id = self.task_box.currentData()
        task = self.task_info.get(int(task_id)) if task_id is not None else None
        if not task:
            return
        self.viewport_x.setValue(int(task.get("canvas_origin_x", 0)))
        self.viewport_y.setValue(int(task.get("canvas_origin_y", 0)))
        self.viewport_width.setValue(int(task.get("canvas_width", 240)))
        self.viewport_height.setValue(int(task.get("canvas_height", 135)))

    def _set_selected_viewport(self):
        task_id = self.task_box.currentData()
        if task_id is None:
            self._show_error("Select a task first.")
            return
        task = self.task_info.get(int(task_id), {})
        self._request("task.viewport", task_id, self.viewport_x.value(),
                      self.viewport_y.value(), self.viewport_width.value(),
                      self.viewport_height.value(),
                      int(task.get("canvas_offset_x", 0)),
                      int(task.get("canvas_offset_y", 0)), callback="viewport")

    def _tile_tasks(self):
        tasks = [task for task in self.task_info.values() if not task.get("supervisor")]
        if not tasks:
            self._show_error("Start at least one worker task first.")
            return
        columns = math.ceil(math.sqrt(len(tasks)))
        rows = math.ceil(len(tasks) / columns)
        screen_width = self.screen_width.value()
        screen_height = self.screen_height.value()
        tile_width = max(1, screen_width // columns)
        tile_height = max(1, screen_height // rows)
        for index, task in enumerate(tasks):
            x = (index % columns) * tile_width
            y = (index // columns) * tile_height
            self._request("task.viewport", task["id"], x, y, tile_width, tile_height,
                          int(task.get("canvas_offset_x", 0)),
                          int(task.get("canvas_offset_y", 0)), callback="viewport")
        self._log(f"Tiling {len(tasks)} tasks in a {columns}×{rows} grid "
                  f"using {tile_width}×{tile_height} canvases")

    def _populate_files(self, entries):
        self.file_tree.clear()
        for entry in entries:
            item = QTreeWidgetItem([
                entry["name"], "" if entry["directory"] else str(entry["size"]),
                "Directory" if entry["directory"] else "File",
            ])
            item.setData(0, Qt.ItemDataRole.UserRole, entry)
            self.file_tree.addTopLevelItem(item)

    def _list_files(self):
        if self.root_box.currentText():
            self.remote_dir = self.path_input.text().strip() or self.root_box.currentText()
            self._request("files.list", self.remote_dir, callback="files")

    def _file_double_clicked(self, item, _column):
        entry = item.data(0, Qt.ItemDataRole.UserRole)
        if entry and entry["directory"]:
            base = self.remote_dir.rstrip("/")
            self.remote_dir = f"{base}/{entry['name']}"
            self.path_input.setText(self.remote_dir)
            self._list_files()

    def _file_selected(self):
        item = self.file_tree.currentItem()
        entry = item.data(0, Qt.ItemDataRole.UserRole) if item else None
        self.current_source_path = self._join_remote(self.remote_dir, entry["name"]) if entry and not entry["directory"] else None

    @staticmethod
    def _join_remote(directory, name):
        if re.match(r"^[A-Za-z][A-Za-z0-9+.-]*:/", name):
            return name
        return directory.rstrip("/") + "/" + name

    def _upload_file(self):
        local, _ = QFileDialog.getOpenFileName(self, "Upload file")
        if not local:
            return
        default = self._join_remote(self.remote_dir, Path(local).name)
        device_path, accepted = QInputDialog.getText(self, "Upload to device", "Device path:", text=default)
        if not accepted or not device_path:
            return
        key = f"upload-{id(local)}"
        self.transfer_jobs[key] = ("upload", device_path)
        self.worker.upload(key, local, device_path)

    def _download_file(self):
        item = self.file_tree.currentItem()
        entry = item.data(0, Qt.ItemDataRole.UserRole) if item else None
        if not entry or entry["directory"]:
            self._show_error("Select a file to download.")
            return
        path = self._join_remote(self.remote_dir, entry["name"])
        local, _ = QFileDialog.getSaveFileName(self, "Download file", entry["name"])
        if local:
            key = f"download-{id(local)}"
            self.transfer_jobs[key] = ("download", path)
            self.worker.download(key, path, local)

    def _start_selected(self):
        if not self.current_source_path:
            self._show_error("Select a BASIC file in the Files tab first.")
            return
        self._prepare_source("start", self.current_source_path)

    def _attach_selected(self):
        task_id = self.task_box.currentData()
        if task_id is None:
            self._show_error("Select a task to attach to.")
            return
        if not self.current_source_path:
            self._show_error("Select the source file for this task in the Files tab first.")
            return
        self._prepare_source("attach", self.current_source_path, task_id)

    def _prepare_source(self, action, remote_path, task_id=None):
        cache = Path(tempfile.gettempdir()) / f"ql32-debug-{Path(remote_path).name}"
        key = f"source-{action}-{id(self)}"
        self.transfer_jobs[key] = (action, remote_path, task_id, str(cache))
        self.worker.download(key, remote_path, str(cache))

    def _breakpoint_changed(self, physical_line, enabled):
        if self.current_task is None or not self.current_source_path:
            return
        line_text = self.editor.document().findBlockByNumber(physical_line - 1).text()
        numbered = re.match(r"^\s*(\d+)\s+", line_text)
        source_line = int(numbered.group(1)) if numbered else physical_line
        self._request("debug.breakpoint", self.current_task, source_line,
                      "true" if enabled else "false")

    def _resume(self, mode):
        if self.current_task is not None:
            self._request("debug.resume", self.current_task, mode)

    def _pause(self):
        if self.current_task is not None:
            self._request("debug.pause", self.current_task)

    def _inspect(self):
        if self.current_task is not None and not self._inspect_inflight:
            self._inspect_inflight = True
            self._request("debug.inspect", self.current_task, callback="inspect")

    def _poll_debug_state(self):
        if self._is_connected and not self.transfer_jobs:
            tasks_pending = any(callback == "tasks" for callback in self.pending.values())
            if not tasks_pending:
                self._request("tasks.list", callback="tasks")
            self._inspect()

    def _show_snapshot(self, snapshot):
        location = snapshot["location"]
        state = "PAUSED" if snapshot.get("paused") else "RUNNING"
        self.where_label.setText(
            f"{state} — BASIC {location.get('basic_line') or '—'}, "
            f"source {location.get('source_line')}:{location.get('column')} "
            f"bytecode {location.get('instruction')}"
        )
        if snapshot.get("paused") and location.get("source_line"):
            self.editor.goto_source_line(location["source_line"])
            self.editor.set_execution_line(location["source_line"])
        else:
            self.editor.set_execution_line(None)
        variables = {"locals": snapshot["frames"][-1]["locals"] if snapshot["frames"] else {},
                     "local_arrays": snapshot["frames"][-1]["arrays"] if snapshot["frames"] else [],
                     "globals": snapshot["globals"], "arrays": snapshot["arrays"]}
        self.variables.setPlainText(json.dumps(variables, indent=2, ensure_ascii=False))
        stack = [
            {"name": frame["name"], "function": frame["function"],
             "return_location": frame["return_location"]}
            for frame in snapshot["frames"]
        ]
        watches = snapshot.get("watches", [])
        stats = snapshot.get("stats", {})
        self.stack_view.setPlainText(json.dumps({"stack": stack, "watches": watches,
                                                 "stats": stats,
                                                 "breakpoints": snapshot.get("breakpoints", [])},
                                                indent=2, ensure_ascii=False))

    def _evaluate(self):
        if self.current_task is None or not self.eval_input.text().strip():
            return
        self._request("debug.eval", self.current_task, self.eval_input.text().strip(), callback="eval")

    def _watch(self, action):
        if self.current_task is not None and self.watch_input.text().strip():
            self._request("debug.watch", self.current_task, action,
                          self.watch_input.text().strip())

    def _toggle_diagnostics(self, enabled):
        if self.current_task is not None:
            self._request("debug.diagnostics", self.current_task,
                          "true" if enabled else "false", "false")

    def _reset_stats(self):
        if self.current_task is not None:
            self._request("debug.reset", self.current_task)

    def _capture_screen(self):
        if not self.screen_capture:
            self._show_error("This board does not support screen capture.")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Capture device screen", "ql32-screen.bmp", "Bitmap (*.bmp)")
        if path:
            key = f"capture-{id(path)}"
            self.transfer_jobs[key] = ("capture", path)
            self.worker.capture(key, path)

    def _save_screen(self):
        if not self._screen_path or not self._screen_path.exists():
            self._show_error("Capture a screen image first.")
            return
        path, _ = QFileDialog.getSaveFileName(self, "Save screen image", self._screen_path.name, "Bitmap (*.bmp)")
        if path:
            Path(path).write_bytes(self._screen_path.read_bytes())

    def _on_transfer_progress(self, key, done, total):
        self.transfer_progress.setValue(0 if not total else int(done * 1000 / total))
        self.status_label.setText(f"Transfer {done}/{total} bytes")

    def _on_transfer_status(self, key, status):
        self.status_label.setText(status)

    def _on_transfer_finished(self, key, kind, path, error):
        job = self.transfer_jobs.pop(key, None)
        self.transfer_progress.setValue(0)
        if self._is_connected and self._connected_port:
            self.status_label.setText(f"Connected: {self._connected_port}")
        if error:
            self._log(f"{kind}: {error}")
            if "cancel" not in error.lower():
                self._show_error(error)
            return
        self._log(f"{kind} complete: {path}")
        if job and job[0] in ("start", "attach"):
            action, remote_path, task_id, cached_source = job
            self.current_source_path = remote_path
            try:
                self.editor.setPlainText(Path(cached_source).read_text(encoding="utf-8"))
            except Exception as exc:
                self._show_error(f"Could not load source text: {exc}")
                return
            if action == "start":
                self._request("task.start", remote_path, callback="start")
            else:
                self._request("debug.attach", task_id, callback="attach")
        elif kind == "capture":
            self._screen_path = Path(path)
            from PySide6.QtGui import QPixmap
            pixmap = QPixmap(path)
            self.screen_view.setPixmap(pixmap.scaled(
                self.screen_view.size(), Qt.AspectRatioMode.KeepAspectRatio,
                Qt.TransformationMode.SmoothTransformation))
            self.screen_status.setText(f"Captured {pixmap.width()}×{pixmap.height()}")
        elif kind in ("upload", "download"):
            self._list_files()

    def _log(self, message):
        text = str(message).strip()
        if text and text != ">":
            self.log_view.appendPlainText(text)

    def _append_repl_output(self, message):
        text = str(message)
        # Debugger polling can terminate a prompt that was already waiting on
        # the serial line. The REPL input has its own field, so a prompt-only
        # line is noise here.
        if text.strip() == ">":
            return
        self.repl_view.appendPlainText(text)

    def _send_repl_command(self):
        command = self.repl_input.text().strip()
        if not command:
            return
        if not self._is_connected:
            self._append_repl_output("Connect to QL/32 before sending commands.")
            return
        self.repl_input.clear()
        self.worker.send_repl_command(command)

    def _show_error(self, message):
        QMessageBox.warning(self, "QL/32 Remote Debugger", str(message))

    def closeEvent(self, event):
        self.worker.stop()
        event.accept()


def main():
    app = QApplication([])
    window = DebuggerWindow()
    window.show()
    return app.exec()

