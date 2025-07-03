# LangChain Gemini
AI project using LangChain and Google Gemini  

## Team Winner
```
uv run winner.py
```
```
> Entering new AgentExecutor chain...
Thought: The user wants to find the team with the most wins from the provided dataframe `df`. I need to identify the 'Wins' column, find the maximum value in that column, and then identify the corresponding 'MLB Team'.

1.  **Inspect the data**: I have the head of the dataframe, which shows the columns 'MLB Team', 'Payroll in millions', and 'Wins'.
2.  **Find the maximum wins**: I will use the `.max()` method on the 'Wins' column to find the highest number of wins.
3.  **Find the winning team**: I will locate the row corresponding to the maximum wins and extract the 'MLB Team' name. A good way to do this is using `idxmax()` to get the index of the maximum value and then using `.loc` to retrieve the data for that row.Action: python_repl_ast
Action Input: df.loc[df['Wins'].idxmax()]MLB Team               Three Team
Payroll in millions         $5.00
Wins                            3
Name: 2, dtype: object
I now know the final answer
Final Answer: The team with the most wins is 'Three Team' with 3 wins.

> Finished chain.
```
--- FINAL ANSWER ---
```xml
<top_winner>
  <MLB_Team>Three Team</MLB_Team>
  <Payroll_in_millions>$5.00</Payroll_in_millions>
  <Wins>3</Wins>
</top_winner>
```

### Sing a ballad of LangChain
```
(Hark, gather 'round, ye coders bold, a tale of modern days be told. Pluck the strings of a synth guitar, and sing of a new and shining star.)

In silicon, a mind did sleep,
A sea of knowledge, vast and deep.
The Large Language Model, grand and great,
But locked behind a stateless gate.
It knew the past, but not the now,
It could not act, nor make a vow,
Confined within its lonely cage,
A brilliant, but a finished page.

(Chorus)
**Oh, hear ye, hear ye, the code resounds,**
**For LangChain, where the future's found!**
**It breaks the box, it sets thoughts free,**
**And links the mind for all to see!**

Then came a framework, born of need,
To plant a new and potent seed.
To give the model hands and eyes,
And hear the world's own true replies.
Its name was whispered, then a shout,
"It's LangChain, let the thinker out!"
It came not with a sword or shield,
But with the logic it could wield.

The first great magic it would weave,
A simple Chain, you must believe.
A Prompt, a Model, then a Parse,
A thread of thought among the stars.
It linked them up, one by one,
A task was started, work was done,
No longer just a single call,
But steps to rise above the wall.

(Chorus)
**Oh, hear ye, hear ye, the code resounds,**
**For LangChain, where the future's found!**
**It breaks the box, it sets thoughts free,**
**And links the mind for all to see!**

But what if thought required a tool?
To break a computational rule?
To search the web for recent news,
Or calculate financial dues?
It forged an Agent, keen and bright,
To choose a path with all its might.
"I need a fact," the Agent cried,
And used a Search API with pride.

A mind that can't recall its past,
Is doomed to make its errors last.
So Memory was its next design,
A conversation, yours and mine.
A buffer stored, a summary known,
So seeds of context could be sown.
The model learned to know your name,
And played a much more clever game.

(Chorus)
**Oh, hear ye, hear ye, the code resounds,**
**For LangChain, where the future's found!**
**It breaks the box, it sets thoughts free,**
**And links the mind for all to see!**

From querying a database deep,
While other, lesser systems sleep,
To reading docs and writing code,
And lightening the developer's load.
The chatbot now could be a clerk,
A researcher who'd do the work,
A helper, partner, and a guide,
With all the world's own truth inside.

So raise a glass, ye builders all,
Who answer innovation's call.
For when an AI seems to know,
The way the winds of data blow,
Remember how the chains were laid,
The silent, brilliant mind unmade,
And then rebuilt, to be unbound,
Upon LangChain's hallowed ground.
```