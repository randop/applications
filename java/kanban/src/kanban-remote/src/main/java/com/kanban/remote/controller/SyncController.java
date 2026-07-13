package com.kanban.remote.controller;

import com.kanban.remote.dto.RemoteCardDto;
import com.kanban.remote.dto.RemoteCardRequest;
import com.kanban.remote.model.RemoteCard;
import com.kanban.remote.repository.RemoteCardRepository;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.time.Instant;
import java.util.NoSuchElementException;

/**
 * Remote sync endpoint. Only accepts ciphertext. encKey / encSalt are never
 * sent here. This service cannot decrypt the title/description fields.
 */
@RestController
@RequestMapping("/api/sync/cards")
public class SyncController {

    private final RemoteCardRepository remoteCardRepository;

    public SyncController(RemoteCardRepository remoteCardRepository) {
        this.remoteCardRepository = remoteCardRepository;
    }

    @PutMapping("/{cardId}")
    public ResponseEntity<RemoteCardDto> upsertCard(@PathVariable String cardId, @RequestBody RemoteCardRequest req) {
        RemoteCard card = remoteCardRepository.findByCardId(cardId)
                .orElseGet(() -> {
                    RemoteCard newCard = new RemoteCard();
                    newCard.setCardId(cardId);
                    return newCard;
                });

        card.setBoardId(req.boardId());
        card.setTitleCipher(req.titleCipher());
        card.setTitleIv(req.titleIv());
        card.setDescCipher(req.descCipher());
        card.setDescIv(req.descIv());
        card.setStatus(req.status());
        card.setPosition(req.position());
        card.setUpdatedAt(req.updatedAt() != null ? req.updatedAt() : Instant.now());

        RemoteCard saved = remoteCardRepository.save(card);
        return ResponseEntity.status(HttpStatus.OK).body(toDto(saved));
    }

    @GetMapping("/{cardId}")
    public ResponseEntity<RemoteCardDto> getCard(@PathVariable String cardId) {
        RemoteCard card = remoteCardRepository.findByCardId(cardId)
                .orElseThrow(() -> new NoSuchElementException("Card not found: " + cardId));
        return ResponseEntity.ok(toDto(card));
    }

    @DeleteMapping("/{cardId}")
    public ResponseEntity<Void> deleteCard(@PathVariable String cardId) {
        remoteCardRepository.findByCardId(cardId).ifPresent(remoteCardRepository::delete);
        return ResponseEntity.noContent().build();
    }

    private RemoteCardDto toDto(RemoteCard card) {
        return new RemoteCardDto(
                card.getId(), card.getCardId(), card.getBoardId(),
                card.getTitleCipher(), card.getTitleIv(),
                card.getDescCipher(), card.getDescIv(),
                card.getStatus(), card.getPosition(),
                card.getCreatedAt(), card.getUpdatedAt()
        );
    }
}
