package com.kanban.local.service;

import com.kanban.local.dto.BoardRequest;
import com.kanban.local.model.Board;
import com.kanban.local.repository.BoardRepository;
import org.springframework.stereotype.Service;

import java.util.List;
import java.util.NoSuchElementException;

@Service
public class BoardService {

    private final BoardRepository boardRepository;

    public BoardService(BoardRepository boardRepository) {
        this.boardRepository = boardRepository;
    }

    public Board create(BoardRequest req) {
        Board board = new Board();
        board.setName(req.name());
        return boardRepository.save(board);
    }

    public Board get(String id) {
        return boardRepository.findById(id)
                .orElseThrow(() -> new NoSuchElementException("Board not found: " + id));
    }

    public List<Board> list() {
        return boardRepository.findAll();
    }

    public void delete(String id) {
        boardRepository.deleteById(id);
    }
}
