package com.kanban.local.service;

import com.kanban.local.crypto.CryptoService;
import com.kanban.local.crypto.EncryptedField;
import com.kanban.local.dto.CardRequest;
import com.kanban.local.dto.CardResponse;
import com.kanban.local.model.Card;
import com.kanban.local.model.SyncStatus;
import com.kanban.local.repository.CardRepository;
import org.springframework.stereotype.Service;

import java.time.Instant;
import java.util.List;
import java.util.NoSuchElementException;

@Service
public class CardService {

    private final CardRepository cardRepository;
    private final CryptoService cryptoService;
    private final SyncService syncService;

    public CardService(CardRepository cardRepository, CryptoService cryptoService, SyncService syncService) {
        this.cardRepository = cardRepository;
        this.cryptoService = cryptoService;
        this.syncService = syncService;
    }

    public CardResponse create(CardRequest req) {
        Card card = new Card();
        card.setBoardId(req.boardId());
        card.setStatus(req.status() == null ? "TODO" : req.status());
        card.setPosition(req.position());

        // Independent key + salt per card, stored only in the local database.
        card.setEncKey(cryptoService.generateRawKey());
        card.setEncSalt(cryptoService.generateSalt());

        applyEncryption(card, req.title(), req.description());

        Card saved = cardRepository.save(card);
        syncService.syncAsync(saved.getId());
        return toResponse(saved);
    }

    public CardResponse update(String id, CardRequest req) {
        Card card = cardRepository.findById(id)
                .orElseThrow(() -> new NoSuchElementException("Card not found: " + id));

        card.setBoardId(req.boardId());
        card.setStatus(req.status());
        card.setPosition(req.position());

        // Re-encrypt with the same key/salt, fresh IV per GCM requirements.
        applyEncryption(card, req.title(), req.description());
        card.setSyncStatus(SyncStatus.PENDING);
        card.setUpdatedAt(Instant.now());

        Card saved = cardRepository.save(card);
        syncService.syncAsync(saved.getId());
        return toResponse(saved);
    }

    public CardResponse get(String id) {
        Card card = cardRepository.findById(id)
                .orElseThrow(() -> new NoSuchElementException("Card not found: " + id));
        return toResponse(card);
    }

    public List<CardResponse> listByBoard(String boardId) {
        return cardRepository.findByBoardId(boardId).stream().map(this::toResponse).toList();
    }

    public void delete(String id) {
        cardRepository.deleteById(id);
        syncService.syncDeleteAsync(id);
    }

    private void applyEncryption(Card card, String title, String description) {
        EncryptedField titleField = cryptoService.encrypt(title, card.getEncKey(), card.getEncSalt());
        card.setTitleCipher(titleField.cipherText());
        card.setTitleIv(titleField.iv());

        EncryptedField descField = cryptoService.encrypt(
                description == null ? "" : description, card.getEncKey(), card.getEncSalt());
        card.setDescCipher(descField.cipherText());
        card.setDescIv(descField.iv());
    }

    private CardResponse toResponse(Card card) {
        String title = cryptoService.decrypt(
                new EncryptedField(card.getTitleCipher(), card.getTitleIv()), card.getEncKey(), card.getEncSalt());
        String description = cryptoService.decrypt(
                new EncryptedField(card.getDescCipher(), card.getDescIv()), card.getEncKey(), card.getEncSalt());

        return new CardResponse(
                card.getId(), card.getBoardId(), title, description, card.getStatus(),
                card.getPosition(), card.getSyncStatus().name(), card.getRemoteId(),
                card.getCreatedAt(), card.getUpdatedAt());
    }
}
