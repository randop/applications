from langchain_google_genai import ChatGoogleGenerativeAI
from langchain_core.messages import HumanMessage

llm = ChatGoogleGenerativeAI(model="gemini-2.5-pro")

def main():
    print("AI project")
    result = llm.invoke("Sing a ballad of LangChain.")
    print(result.content)

if __name__ == "__main__":
    main()
