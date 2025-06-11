#ifndef BLOG_POST_H
#define BLOG_POST_H

namespace blog {
class Post {
public:
  // Constructor taking a shared_ptr to ConnectionPool
  explicit Post(std::shared_ptr<db::ConnectionPool> sharedPool);

  // Default destructor
  ~Post() = default;

private:
  std::shared_ptr<db::ConnectionPool> pool;
};

Post::Post(std::shared_ptr<db::ConnectionPool> sharedPool)
    : pool(std::move(sharedPool)) {
  if (!pool) {
    throw std::invalid_argument("Connection pool cannot be null");
  }
}

} // namespace blog

#endif // BLOG_POST_H
