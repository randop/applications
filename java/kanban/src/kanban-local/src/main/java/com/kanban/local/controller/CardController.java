package com.kanban.local.controller;

import com.kanban.local.dto.CardRequest;
import com.kanban.local.dto.CardResponse;
import com.kanban.local.service.CardService;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;

@RestController
@RequestMapping("/api/cards")
public class CardController {

    private final CardService cardService;

    public CardController(CardService cardService) {
        this.cardService = cardService;
    }

    @PostMapping
    public ResponseEntity<CardResponse> create(@RequestBody CardRequest req) {
        return ResponseEntity.status(HttpStatus.CREATED).body(cardService.create(req));
    }

    @PutMapping("/{id}")
    public ResponseEntity<CardResponse> update(@PathVariable String id, @RequestBody CardRequest req) {
        return ResponseEntity.ok(cardService.update(id, req));
    }

    @GetMapping("/{id}")
    public ResponseEntity<CardResponse> get(@PathVariable String id) {
        return ResponseEntity.ok(cardService.get(id));
    }

    @GetMapping
    public ResponseEntity<List<CardResponse>> listByBoard(@RequestParam String boardId) {
        return ResponseEntity.ok(cardService.listByBoard(boardId));
    }

    @DeleteMapping("/{id}")
    public ResponseEntity<Void> delete(@PathVariable String id) {
        cardService.delete(id);
        return ResponseEntity.noContent().build();
    }
}
