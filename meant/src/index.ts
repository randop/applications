const log = console;

import * as express from "express";
import { Application, Router, Request, Response } from "express";

const app: Application = express();
const PORT: number = 3000;
const HOST: string = "0.0.0.0";

app.use(express.json());

const router = Router();

router.get("/", async (req: Request, res: Response) => {
  res.send({ data: "Hello World!", request: req.url });
});

app.use(router);

app.listen(PORT, HOST, () => {
  log.info(`Server running on http://${HOST}:${PORT}`);
});
