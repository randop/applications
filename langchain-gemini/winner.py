import os
import pandas as pd
from langchain_google_genai import ChatGoogleGenerativeAI
from langchain_experimental.agents.agent_toolkits import create_pandas_dataframe_agent

# --- Best Practice: Load API key from environment variables ---
# In your terminal, run: export GOOGLE_API_KEY="your_api_key_here"
# This is more secure than hardcoding the key.
api_key = os.getenv("GOOGLE_API_KEY")
if not api_key:
    raise ValueError("GOOGLE_API_KEY environment variable not set!")

df = pd.read_csv("./data/mlb_teams_2012.csv")

llm = ChatGoogleGenerativeAI(
    model="gemini-2.5-pro",
    google_api_key=api_key,
    temperature=0  # Use a lower temperature for more predictable, factual answers from data
)

agent = create_pandas_dataframe_agent(
    llm,
    df,
    verbose=True,
    allow_dangerous_code=True
)

prompt = "Who is the top winner? First, identify the column that represents wins. Then, find the team with the maximum value in that column. Let's do it step by step. The final result must be the full row for that team, presented in an XML format."
result = agent.invoke(prompt)

# Print the final answer from the agent
print("\n--- FINAL ANSWER ---")
print(result['output'])
