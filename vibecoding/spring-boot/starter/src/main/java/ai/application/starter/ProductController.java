package ai.application.starter;

import java.util.List;
import org.springframework.cache.annotation.CacheEvict;
import org.springframework.cache.annotation.Cacheable;
import org.springframework.web.bind.annotation.*;

@RestController
@RequestMapping("/products")
public class ProductController {
    private final ProductRepository repository;

    public ProductController(ProductRepository repository) {
        this.repository = repository;
    }

    @GetMapping
    @Cacheable(value = "products")
    public List<Product> getAllProducts() {
        return repository.findAll();
    }

    @PostMapping
    @CacheEvict(value = "products", allEntries = true)
    public Product createProduct(@RequestBody Product product) {
        return repository.save(product);
    }
}
