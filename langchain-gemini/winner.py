import pandas as pd
from langchain.agents.agent_types import AgentType
from langchain_experimental.agents.agent_toolkits import create_pandas_dataframe_agent
from langchain.callbacks.streaming_stdout import StreamingStdOutCallbackHandler
from langchain_google_genai import ChatGoogleGenerativeAI
from langchain_core.messages import HumanMessage

callbacks = [StreamingStdOutCallbackHandler()]

df = pd.read_csv("./data/mlb_teams_2012.csv")

llm = ChatGoogleGenerativeAI(model="gemini-2.5-pro")

agent = create_pandas_dataframe_agent(
    llm,
    df,
    verbose=True,
    allow_dangerous_code=True
)

agent.invoke("Who is top winner? Let's do it step by step.")
