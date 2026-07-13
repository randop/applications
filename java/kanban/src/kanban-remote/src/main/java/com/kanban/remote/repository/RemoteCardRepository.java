package com.kanban.remote.repository;

import com.kanban.remote.model.RemoteCard;
import org.springframework.data.mongodb.repository.MongoRepository;

import java.util.List;
import java.util.Optional;

public interface RemoteCardRepository extends MongoRepository<RemoteCard, String> {
    List<RemoteCard> findByBoardId(String boardId);
    Optional<RemoteCard> findByCardId(String cardId);
}
