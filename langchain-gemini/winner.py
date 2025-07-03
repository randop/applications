import os
import pandas as pd
import xml.etree.ElementTree as ET
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
    verbose=False,
    allow_dangerous_code=True
)

prompt = """
Who is top winner? Let's do it step by step.
MUST format the final answer as an XML string.
The XML structure must be exactly: <result><winner>$TEAM</winner></result>

Replace $TEAM with the actual name of the team.
Do not include any other text, markdown, explanations, code, or any non xml formatting in your final answer. The output should ONLY be the XML string.
"""

def parseResult(xmlData):
    root = ET.fromstring(xmlData)
    print("\nWinner: " + root.find('winner').text)

def main():
    print("Team Winner AI")
    result = agent.invoke(prompt)

    # Print the final answer from the agent
    print("\n--- RESULT ---")
    xmlData = '<?xml version="1.0" encoding="UTF-8"?>' + result['output']
    print(xmlData)
    parseResult(xmlData)

if __name__ == "__main__":
    main()
