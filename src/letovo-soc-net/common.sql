-- common sql requests

-- list users chats
SELECT g.groupName, utg.groupId, usm.fromUser, usm.messageText, usm.messageTime
FROM userGroup as g
INNER JOIN userToGroup as utg
    ON g.groupId = utg.groupId
INNER JOIN user as u
    ON u.userId = utg.userId
INNER JOIN 
    SELECT um.toGroup, um.fromUser, um.messageText, messageTime
    FROM userMessage um
    INNER JOIN (
        SELECT toGroup, max(messageTime) as maxTime
        FROM userMessage
        GROUP BY toGroup
    ) msgs ON um.toGroup = msgs.toGroup and um.messageTime = msgs.maxTime as usm
    ON usm.toGroup = g.groupId
WHERE u.userId = ?
ORDER BY usm.messageTime DESC;