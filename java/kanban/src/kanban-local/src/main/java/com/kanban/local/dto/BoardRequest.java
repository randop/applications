package com.kanban.local.dto;

import jakarta.validation.constraints.NotBlank;

public record BoardRequest(@NotBlank String name) {}
