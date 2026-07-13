package com.kanban.local.controller;

import com.kanban.local.dto.BoardRequest;
import com.kanban.local.model.Board;
import com.kanban.local.service.BoardService;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/api/boards")
public class BoardController {

    private final BoardService boardService;

    public BoardController(BoardService boardService) {
        this.boardService = boardService;
    }

    @PostMapping
    public ResponseEntity<Board> create(@RequestBody BoardRequest req) {
        return ResponseEntity.status(HttpStatus.CREATED).body(boardService.create(req));
    }

    @GetMapping("/{id}")
    public ResponseEntity<Board> get(@PathVariable String id) {
        return ResponseEntity.ok(boardService.get(id));
    }

    @GetMapping
    public ResponseEntity<List<Board>> list() {
        return ResponseEntity.ok(boardService.list());
    }

    @DeleteMapping("/{id}")
    public ResponseEntity<Void> delete(@PathVariable String id) {
        boardService.delete(id);
        return ResponseEntity.noContent().build();
    }
}
