package com.kanban.local.repository;

import com.kanban.local.model.Card;
import com.kanban.local.model.SyncStatus;
import org.springframework.data.mongodb.repository.MongoRepository;

import java.util.List;

public interface CardRepository extends MongoRepository<Card, String> {
    List<Card> findByBoardId(String boardId);
    List<Card> findBySyncStatusIn(List<SyncStatus> statuses);
}
