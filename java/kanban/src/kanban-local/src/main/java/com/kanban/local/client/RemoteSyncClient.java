package com.kanban.local.client;

import com.kanban.local.dto.RemoteCardPayload;
import com.kanban.local.model.Card;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

@Component
public class RemoteSyncClient {

    private final RestClient restClient;

    public RemoteSyncClient(RestClient remoteRestClient) {
        this.restClient = remoteRestClient;
    }

    // encKey / encSalt are deliberately absent from this payload - the
    // remote service only ever receives ciphertext + iv.
    public String pushCard(Card card) {
        RemoteCardPayload payload = new RemoteCardPayload(
                card.getId(),
                card.getBoardId(),
                card.getTitleCipher(),
                card.getTitleIv(),
                card.getDescCipher(),
                card.getDescIv(),
                card.getStatus(),
                card.getPosition(),
                card.getUpdatedAt()
        );

        RemoteCardPayload response = restClient.put()
                .uri("/api/sync/cards/{cardId}", card.getId())
                .body(payload)
                .retrieve()
                .body(RemoteCardPayload.class);

        return response != null ? response.cardId() : card.getId();
    }

    public void deleteCard(String cardId) {
        restClient.delete().uri("/api/sync/cards/{cardId}", cardId).retrieve().toBodilessEntity();
    }
}
