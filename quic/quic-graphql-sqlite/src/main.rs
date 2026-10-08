use async_graphql::{Context, EmptySubscription, Object, Request as GqlRequest, Schema};
use salvo::conn::rustls::{Keycert, RustlsConfig};
use salvo::prelude::*;
use sqlx::{FromRow, SqlitePool};
use std::fs;

#[derive(Clone, async_graphql::SimpleObject, FromRow)]
struct User {
    id: i64,
    name: String,
}

struct QueryRoot;

#[Object]
impl QueryRoot {
    async fn users(&self, ctx: &Context<'_>) -> async_graphql::Result<Vec<User>> {
        let pool = ctx.data::<SqlitePool>()?;
        let users = sqlx::query_as::<_, User>("SELECT id, name FROM users")
            .fetch_all(pool)
            .await?;
        Ok(users)
    }
}

struct MutationRoot;

#[Object]
impl MutationRoot {
    async fn add_user(&self, ctx: &Context<'_>, name: String) -> async_graphql::Result<User> {
        let pool = ctx.data::<SqlitePool>()?;
        let result = sqlx::query("INSERT INTO users (name) VALUES (?)")
            .bind(&name)
            .execute(pool)
            .await?;
        
        Ok(User {
            id: result.last_insert_rowid(),
            name,
        })
    }
}

type AppSchema = Schema<QueryRoot, MutationRoot, EmptySubscription>;

#[handler]
async fn graphql_playground(res: &mut Response) {
    use async_graphql::http::{playground_source, GraphQLPlaygroundConfig};
    let html = playground_source(GraphQLPlaygroundConfig::new("/graphql"));
    res.render(Text::Html(html));
}

#[handler]
async fn graphql_post(req: &mut Request, res: &mut Response, depot: &mut Depot) {
    if let Ok(schema) = depot.obtain::<AppSchema>() {
        if let Ok(gql_req) = req.parse_json::<GqlRequest>().await {
            let gql_resp = schema.execute(gql_req).await;
            res.render(Json(gql_resp));
        } else {
            res.status_code(StatusCode::BAD_REQUEST);
        }
    }
}

#[tokio::main]
async fn main() -> anyhow::Result<()> {
    tracing_subscriber::fmt().init();

    let pool = SqlitePool::connect("sqlite::memory:").await?;
    sqlx::query(
        "CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL
        );"
    )
    .execute(&pool)
    .await?;

    let schema = Schema::build(QueryRoot, MutationRoot, EmptySubscription)
        .data(pool)
        .finish();

    let router = Router::new()
        .hoop(salvo::affix_state::inject(schema))
        .push(
            Router::with_path("graphql")
                .get(graphql_playground)
                .post(graphql_post),
        );

    let cert = fs::read("cert.pem").expect("Ensure cert.pem exists");
    let key = fs::read("key.pem").expect("Ensure key.pem exists");
    let config = RustlsConfig::new(Keycert::new().cert(cert).key(key));

    let addr = ("0.0.0.0", 8443);

    let tcp_listener = TcpListener::new(addr).rustls(config.clone());
    let acceptor = QuinnListener::new(config.build_quinn_config().unwrap(), addr)
        .join(tcp_listener)
        .bind()
        .await;

    tracing::info!("🚀 Server running on https://127.0.0.1:8443");
    tracing::info!("Try the GraphQL playground at https://127.0.0.1:8443/graphql");

    Server::new(acceptor).serve(router).await;

    Ok(())
}
