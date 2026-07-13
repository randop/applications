package com.kanban.local.dto;

import java.time.Instant;

public record CardResponse(
        String id,
        String boardId,
        String title,
        String description,
        String status,
        int position,
        String syncStatus,
        String remoteId,
        Instant createdAt,
        Instant updatedAt
) {}
