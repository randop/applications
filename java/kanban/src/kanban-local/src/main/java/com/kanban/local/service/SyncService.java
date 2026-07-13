package com.kanban.local.service;

import com.kanban.local.client.RemoteSyncClient;
import com.kanban.local.config.SyncProperties;
import com.kanban.local.model.Card;
import com.kanban.local.model.SyncStatus;
import com.kanban.local.repository.CardRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.scheduling.annotation.Async;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Service;

import java.util.List;

/**
 * Pushes ciphertext-only payloads to the remote kanban API on a schedule
 * and on write. encKey / encSalt never leave this process.
 */
@Service
public class SyncService {

    private static final Logger log = LoggerFactory.getLogger(SyncService.class);

    private final CardRepository cardRepository;
    private final RemoteSyncClient remoteSyncClient;
    private final SyncProperties syncProperties;

    public SyncService(CardRepository cardRepository, RemoteSyncClient remoteSyncClient, SyncProperties syncProperties) {
        this.cardRepository = cardRepository;
        this.remoteSyncClient = remoteSyncClient;
        this.syncProperties = syncProperties;
    }

    @Async
    public void syncAsync(String cardId) {
        cardRepository.findById(cardId).ifPresent(this::pushCard);
    }

    @Async
    public void syncDeleteAsync(String cardId) {
        try {
            remoteSyncClient.deleteCard(cardId);
        } catch (Exception e) {
            log.warn("Remote delete failed for card {}: {}", cardId, e.getMessage());
        }
    }

    @Scheduled(fixedDelayString = "${sync.fixed-delay-ms:30000}")
    public void syncPending() {
        if (!syncProperties.isEnabled()) {
            return;
        }
        List<Card> pending = cardRepository.findBySyncStatusIn(List.of(SyncStatus.PENDING, SyncStatus.FAILED));
        pending.forEach(this::pushCard);
    }

    private void pushCard(Card card) {
        try {
            String remoteId = remoteSyncClient.pushCard(card);
            card.setRemoteId(remoteId);
            card.setSyncStatus(SyncStatus.SYNCED);
        } catch (Exception e) {
            log.warn("Sync failed for card {}: {}", card.getId(), e.getMessage());
            card.setSyncStatus(SyncStatus.FAILED);
        }
        cardRepository.save(card);
    }
}
